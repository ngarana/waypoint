#include "waylaunch/providers/file_provider.h"
#include "waylaunch/clipboard.h"
#include "waylaunch/content/store.h"
#include "waylaunch/history.h"
#include "waylaunch/search_util.h"
#include "waylaunch/subprocess.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <queue>
#include <string_view>
#include <sys/stat.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace waylaunch {

namespace {

// The walk visits directories breadth-first and gives up at whichever bound
// hits first. It is only the fallback/complement to the index now, but it
// still runs on every keystroke, so it must stay bounded.
constexpr std::size_t kMaxVisited = 200000;
constexpr int kDeadlineMs = 300;
// Index candidates per kind (files, folders) before the hidden/exclude filter
// and final ranking. Generous, because hidden trees (.claude worktrees, .github)
// are filtered only after the SQL cap.
constexpr int kIndexCandidates = 1000;

std::string to_lower(std::string s) {
    std::ranges::transform(s, s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool matches_exclude(std::string_view name, std::string_view pat) {
    const bool has_glob = pat.find_first_of("*?[") != std::string_view::npos;
    if (!has_glob) return name == pat;

    const auto m = static_cast<long>(name.size());
    const auto n = static_cast<long>(pat.size());
    std::vector<std::vector<bool>> dp(n + 1, std::vector<bool>(m + 1, false));
    dp[0][0] = true;
    for (long i = 1; i <= n && pat[i - 1] == '*'; ++i) dp[i][0] = true;
    for (long i = 1; i <= n; ++i) {
        for (long j = 1; j <= m; ++j) {
            const char pc = pat[i - 1];
            if (pc == '*') dp[i][j] = dp[i - 1][j] || dp[i][j - 1];
            else if (pc == '?') dp[i][j] = dp[i - 1][j - 1];
            else dp[i][j] = dp[i - 1][j - 1] && pc == name[j - 1];
        }
    }
    return dp[n][m];
}

std::string strip_trailing_slash(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

// `p` lies strictly below `root` (both without trailing slash, root may be "/").
bool strictly_under(const std::string& p, const std::string& root) {
    if (p.size() <= root.size() || !p.starts_with(root)) return false;
    return root == "/" || p[root.size()] == '/';
}

bool at_or_under(const std::string& p, const std::string& root) {
    return p == root || strictly_under(p, root);
}

std::string basename_of(const std::string& p) {
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

// Excludes split by kind: basename patterns match any entry's name; path
// patterns ("go/pkg") match the entry's last N path components.
struct Excludes {
    std::vector<std::string> names;
    std::vector<std::pair<std::string, int>> paths; // pattern, component count

    explicit Excludes(const std::vector<std::string>& pats) {
        for (const auto& p : pats) {
            std::string pat = strip_trailing_slash(p);
            while (pat.starts_with('/')) pat.erase(0, 1);
            if (pat.empty()) continue;
            if (pat.find('/') == std::string::npos) {
                names.push_back(std::move(pat));
            } else {
                int n = 1 + static_cast<int>(std::ranges::count(pat, '/'));
                paths.emplace_back(std::move(pat), n);
            }
        }
    }

    bool excluded(const std::string& full_path, std::string_view name) const {
        for (const auto& pat : names)
            if (matches_exclude(name, pat)) return true;
        for (const auto& [pat, n] : paths) {
            // Start of the last n components of full_path.
            size_t pos = full_path.size();
            int seen = 0;
            while (seen < n && pos > 0) {
                pos = full_path.find_last_of('/', pos - 1);
                if (pos == std::string::npos) break;
                ++seen;
            }
            if (seen < n || pos == std::string::npos) continue;
            if (matches_exclude(std::string_view(full_path).substr(pos + 1), pat)) return true;
        }
        return false;
    }
};

// Would the walk from `base` have reached `path`? Applies the hidden and
// exclude rules to every component below base, so index hits obey exactly the
// same policy as walked ones.
bool admitted_below(const std::string& path, const std::string& base, bool hidden_ok,
                    const Excludes& ex) {
    size_t start = (base == "/") ? 1 : base.size() + 1;
    while (start < path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string_view comp(path.data() + start, end - start);
        if (!hidden_ok && comp.starts_with('.')) return false;
        if (ex.excluded(path.substr(0, end), comp)) return false;
        start = end + 1;
    }
    return true;
}

float score_hit(const std::string& name_lower, size_t pos, const std::string& path, bool is_dir,
                bool have_mtime, std::time_t mtime, const HistoryStore* history) {
    float s = (pos == 0) ? 1000.0F - static_cast<float>(std::min<size_t>(name_lower.size(), 300))
                         : 600.0F - static_cast<float>(std::min<size_t>(pos, 300));
    s -= static_cast<float>(path_depth(path)) * 6.0F;
    if (is_dir) s += 15.0F;
    if (have_mtime) s += recency_bonus(mtime);
    if (history) s += history->frecency(path);
    return s;
}

ListItem make_item(const std::string& path, std::string name, bool is_dir, float score) {
    ListItem it;
    it.kind = is_dir ? ItemKind::Folder : ItemKind::File;
    it.name = std::move(name);
    it.path = path;
    it.description = abbreviate_home(path);
    it.icon_name = is_dir ? "folder" : icon_for_file(path);
    it.score = score;
    return it;
}

} // namespace

FileProvider::FileProvider(FileProviderOptions opts) : opts_(std::move(opts)) {
    for (auto& r : opts_.roots) r = strip_trailing_slash(r);
    for (auto& r : opts_.index_roots) r = strip_trailing_slash(r);
    std::erase_if(opts_.index_roots, [](const std::string& r) { return r.empty(); });
}

bool FileProvider::is_available() const { return true; }

std::vector<ListItem> FileProvider::query(const ProviderQuery& q) {
    if (std::cmp_less(q.text.size(), opts_.min_query)) return {};

    const std::string& ql = q.lower;
    const bool hidden_ok = opts_.include_hidden || ql.starts_with('.');
    const Excludes ex(opts_.excludes);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kDeadlineMs);
    std::size_t visited = 0;

    struct Hit {
        ListItem item;
        bool verify; // from the index: confirm it still exists before showing it
    };
    std::vector<Hit> hits;
    std::unordered_set<std::string> seen; // overlapping roots must not double-list

    // Index lookup for the tree `scope`, filtered as if walked from `base`.
    auto from_index = [&](const std::string& scope, const std::string& base) {
        std::vector<content::NameHit> found;
        if (!opts_.index->search_names(scope, ql, kIndexCandidates, found)) return false;
        for (auto& h : found) {
            if (!admitted_below(h.path, base, hidden_ok, ex)) continue;
            std::string name = basename_of(h.path);
            std::string nl = to_lower(name);
            size_t pos = nl.find(ql);
            if (pos == std::string::npos) continue;
            if (!seen.insert(h.path).second) continue;
            float s = score_hit(nl, pos, h.path, h.is_dir, h.mtime_s > 0,
                                static_cast<std::time_t>(h.mtime_s), opts_.history);
            hits.push_back(
                {.item = make_item(h.path, std::move(name), h.is_dir, s), .verify = true});
        }
        return true;
    };

    // Breadth-first walk of `root`, not descending into `prune` (trees the
    // index already answered). Returns false once the shared budget is spent.
    auto walk = [&](const std::string& root, const std::vector<std::string>& prune) {
        std::queue<std::string> dirs;
        dirs.push(root);
        while (!dirs.empty()) {
            if (++visited > kMaxVisited) return false;
            if ((visited & 1023U) == 0 && std::chrono::steady_clock::now() > deadline) return false;

            std::string dir = std::move(dirs.front());
            dirs.pop();

            std::error_code ec;
            std::filesystem::directory_iterator it(dir, ec);
            std::filesystem::directory_iterator end;
            if (ec) continue;
            for (; it != end; it.increment(ec)) {
                if (ec) break;
                const auto& entry = *it;
                std::string name = entry.path().filename().string();
                if (name == "." || name == "..") continue;
                if (!hidden_ok && name.starts_with('.')) continue;

                std::string path = entry.path().string();
                if (ex.excluded(path, name)) continue;

                bool is_dir = entry.is_directory(ec);
                if (ec) {
                    ec.clear();
                    struct stat st;
                    if (stat(path.c_str(), &st) != 0) continue;
                    is_dir = S_ISDIR(st.st_mode);
                    if (!is_dir && !S_ISREG(st.st_mode)) continue;
                }
                const bool descend = is_dir && std::ranges::find(prune, path) == prune.end();

                std::string nl = to_lower(name);
                size_t pos = nl.find(ql);
                if (pos != std::string::npos && seen.insert(path).second) {
                    struct stat st{};
                    bool have_stat = (stat(path.c_str(), &st) == 0);
                    if (have_stat) is_dir = S_ISDIR(st.st_mode);
                    float s =
                        score_hit(nl, pos, path, is_dir, have_stat, st.st_mtime, opts_.history);
                    hits.push_back(
                        {.item = make_item(path, std::move(name), is_dir, s), .verify = false});
                }
                if (descend) dirs.push(std::move(path));
            }
        }
        return true;
    };

    const bool use_index = opts_.index != nullptr && !opts_.index_roots.empty();
    bool budget_left = true;
    for (const auto& root : opts_.roots) {
        if (root.empty()) continue;
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) continue;

        // The root itself is a hit too (typing "downloads" opens ~/Downloads)
        // unless another root contains it, where it's an ordinary entry.
        const bool top_level = std::ranges::none_of(
            opts_.roots, [&](const std::string& o) { return strictly_under(root, o); });
        if (top_level) {
            std::string name = basename_of(root);
            std::string nl = to_lower(name);
            size_t pos = nl.find(ql);
            if (!name.empty() && pos != std::string::npos && seen.insert(root).second) {
                struct stat st{};
                bool have_stat = (stat(root.c_str(), &st) == 0);
                float s = score_hit(nl, pos, root, true, have_stat, st.st_mtime, opts_.history);
                hits.push_back(
                    {.item = make_item(root, std::move(name), true, s), .verify = false});
            }
        }

        std::vector<std::string> prune;
        if (use_index) {
            // Root inside an indexed tree: the index answers it completely.
            bool inside = std::ranges::any_of(
                opts_.index_roots, [&](const std::string& r) { return at_or_under(root, r); });
            if (inside && from_index(root, root)) continue;
            // Indexed trees inside the root: answer those from the index and
            // walk only the rest.
            if (!inside) {
                for (const auto& r : opts_.index_roots)
                    if (strictly_under(r, root) && from_index(r, root)) prune.push_back(r);
            }
        }
        if (budget_left) budget_left = walk(root, prune);
    }

    std::ranges::stable_sort(
        hits, [](const Hit& a, const Hit& b) { return a.item.score > b.item.score; });
    const auto max = static_cast<size_t>(std::max(1, opts_.max_results));
    std::vector<ListItem> out;
    out.reserve(std::min(max, hits.size()));
    for (auto& h : hits) {
        if (out.size() >= max) break;
        if (h.verify) {
            // The index trails the filesystem by up to an inotify round-trip
            // (or more if the daemon is down): drop vanished paths, fix kinds.
            struct stat st;
            if (stat(h.item.path.c_str(), &st) != 0) continue;
            const bool is_dir = S_ISDIR(st.st_mode);
            if (is_dir != (h.item.kind == ItemKind::Folder)) {
                h.item.kind = is_dir ? ItemKind::Folder : ItemKind::File;
                h.item.icon_name = is_dir ? "folder" : icon_for_file(h.item.path);
            }
        }
        out.push_back(std::move(h.item));
    }
    return out;
}

bool FileProvider::activate(const ListItem& it) {
    if (it.kind != ItemKind::File && it.kind != ItemKind::Folder) return false;
    if (!it.path.empty()) {
        Clipboard::copy_file_path(it.path, event_loop_);
        Subprocess::launch(event_loop_, {"xdg-open", it.path});
    }
    return true;
}

} // namespace waylaunch
