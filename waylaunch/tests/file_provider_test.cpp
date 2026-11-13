#include "waylaunch/content/store.h"
#include "waylaunch/providers/file_provider.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace waylaunch;

namespace fs = std::filesystem;

namespace {

std::string unique_dir(const char* tag) {
    auto p = fs::temp_directory_path() /
             (std::string("waylaunch-fp-test-") + tag + "-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(p);
    return p.string();
}

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << "x";
}

ProviderQuery mkq(const std::string& text) {
    std::string lower = text;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ProviderQuery{.text = text, .lower = lower, .max_results = 6};
}

std::vector<std::string> names_of(const std::vector<ListItem>& r) {
    std::vector<std::string> out;
    out.reserve(r.size());
    for (const auto& it : r) out.push_back(it.name);
    return out;
}

bool has_path(const std::vector<ListItem>& r, const fs::path& p) {
    return std::ranges::any_of(r, [&](const ListItem& it) { return it.path == p.string(); });
}

FileProvider walker(std::vector<std::string> roots, std::vector<std::string> excludes,
                    int min_query, int max_results, bool hidden = false) {
    return FileProvider(FileProviderOptions{
        .roots = std::move(roots),
        .excludes = std::move(excludes),
        .min_query = min_query,
        .max_results = max_results,
        .include_hidden = hidden,
        .history = nullptr,
        .index = nullptr,
        .index_roots = {},
    });
}

// Index a file (it need not exist on disk — that's how the tests tell an index
// answer from a walked one).
void index_put(content::Store& s, const fs::path& p) {
    content::FileRecord r;
    r.path = p.string();
    r.name = p.filename().string();
    r.parent = p.parent_path().string();
    r.size = 1;
    r.mtime_ns = 1000000000LL;
    r.state = content::FileState::Indexed;
    bool ok = s.put(r, "");
    assert(ok);
    (void) ok;
}

} // namespace

int main() {
    std::error_code ec;
    std::string root = unique_dir("root");
    auto cleanup = [&] { fs::remove_all(root, ec); };

    touch(fs::path(root) / "apple.txt");
    touch(fs::path(root) / "banana.txt");
    touch(fs::path(root) / "APRICOT.md");
    touch(fs::path(root) / "not_a_match.dat");
    fs::create_directories(fs::path(root) / "apple_docs");
    touch(fs::path(root) / "apple_docs" / "manifest.txt");

    fs::create_directories(fs::path(root) / "node_modules");
    touch(fs::path(root) / "node_modules" / "apple_hidden.txt");

    fs::create_directories(fs::path(root) / ".git");
    touch(fs::path(root) / ".git" / "apple_also_hidden.txt");

    touch(fs::path(root) / "notes.tmp");
    touch(fs::path(root) / "apple.tmp");

    {
        auto prov = walker({root}, {".git", "node_modules", "*.tmp"}, 2, 6);
        assert(prov.is_available());

        auto r = prov.query(mkq("apple"));
        auto names = names_of(r);
        assert(names.size() == 2);
        assert(names[0] == "apple_docs");
        assert(r[0].kind == ItemKind::Folder);
        assert(names[1] == "apple.txt");
        assert(r[1].kind == ItemKind::File);

        assert(prov.query(mkq("x")).empty());

        auto dir = prov.query(mkq("apple_docs"));
        assert(dir.size() == 1);
        assert(dir[0].kind == ItemKind::Folder);
    }

    {
        auto prov = walker({root}, {".git", "node_modules"}, 2, 100);
        auto r = prov.query(mkq("apple"));
        auto names = names_of(r);
        assert(names.size() == 3);
        assert(names[0] == "apple_docs");
        assert(std::ranges::find(names, "apple.txt") != names.end());
        assert(std::ranges::find(names, "apple.tmp") != names.end());

        auto apr = prov.query(mkq("APR"));
        assert(apr.size() == 1);
        assert(apr[0].name == "APRICOT.md");

        auto pref = prov.query(mkq("Ban"));
        assert(pref.size() == 1);
        assert(pref[0].name == "banana.txt");
    }

    {
        auto prov = walker({root}, {}, 2, 2);
        auto all = prov.query(mkq("ap"));
        assert(all.size() == 2);
        assert(all[0].name == "apple_docs");
    }

    {
        auto prov = walker({"/no/such/path/"}, {}, 1, 6);
        assert(prov.query(mkq("x")).empty());
    }

    {
        auto nope = walker({}, {}, 1, 6);
        assert(nope.query(mkq("apple")).empty());
    }

    // --- Hidden entries: skipped unless opted in or the query starts with '.' ---
    touch(fs::path(root) / ".dotdir" / "apple_in_dot.txt");
    touch(fs::path(root) / ".apple_rc");
    {
        auto prov = walker({root}, {".git", "node_modules"}, 2, 100);
        auto r = prov.query(mkq("apple"));
        assert(!has_path(r, fs::path(root) / ".dotdir" / "apple_in_dot.txt"));
        assert(!has_path(r, fs::path(root) / ".apple_rc"));
        // Excludes stay in force even when hidden entries are allowed.
        assert(!has_path(r, fs::path(root) / "node_modules" / "apple_hidden.txt"));

        auto dot = prov.query(mkq(".apple"));
        assert(dot.size() == 1 && dot[0].name == ".apple_rc");

        auto with_hidden = walker({root}, {".git", "node_modules"}, 2, 100, /*hidden=*/true);
        auto h = with_hidden.query(mkq("apple"));
        assert(has_path(h, fs::path(root) / ".dotdir" / "apple_in_dot.txt"));
        assert(has_path(h, fs::path(root) / ".apple_rc"));
        assert(!has_path(h, fs::path(root) / ".git" / "apple_also_hidden.txt"));

        // A hidden root itself is still searched (only entries below it count).
        auto in_dot = walker({(fs::path(root) / ".dotdir").string()}, {}, 2, 6);
        assert(in_dot.query(mkq("apple")).size() == 1);
    }

    // --- A top-level root matches by its own name; a nested root doesn't double up ---
    {
        const fs::path docs = fs::path(root) / "apple_docs";
        auto top = walker({docs.string()}, {}, 2, 6);
        auto r = top.query(mkq("apple_d"));
        assert(r.size() == 1 && r[0].path == docs.string() && r[0].kind == ItemKind::Folder);

        auto nested = walker({root, docs.string()}, {".git", "node_modules"}, 2, 100);
        auto n = nested.query(mkq("apple_d"));
        assert(n.size() == 1 && n[0].path == docs.string());
    }

    // --- Path-pattern excludes match trailing components, not just names ---
    touch(fs::path(root) / "proj" / "backups" / "apple_backup.txt");
    touch(fs::path(root) / "other" / "backups" / "apple_kept.txt");
    {
        auto prov = walker({root}, {".git", "node_modules", "proj/backups/"}, 2, 100);
        auto r = prov.query(mkq("apple_"));
        assert(!has_path(r, fs::path(root) / "proj" / "backups" / "apple_backup.txt"));
        assert(has_path(r, fs::path(root) / "other" / "backups" / "apple_kept.txt"));

        auto glob = walker({root}, {".git", "node_modules", "*/backups"}, 2, 100);
        auto g = glob.query(mkq("apple_"));
        assert(!has_path(g, fs::path(root) / "proj" / "backups" / "apple_backup.txt"));
        assert(!has_path(g, fs::path(root) / "other" / "backups" / "apple_kept.txt"));
    }

    // --- Index-backed search ---
    {
        const fs::path idx = fs::path(root) / "indexed";
        const fs::path deep = idx / "court" / "reports" / "adhoc" / "2026-10-05_mc";
        touch(deep / "report.csv");                          // on disk + indexed
        touch(idx / "court" / "walk_only_adhoc.txt");        // on disk, NOT indexed
        touch(idx / ".claude" / "adhoc_copy" / "f.txt");     // hidden, indexed
        touch(idx / "court" / "exclude_me" / "adhoc_x.txt"); // excluded, indexed
        touch(fs::path(root) / "outside_adhoc.txt");         // outside the index root

        content::Store store;
        bool opened = store.open((fs::path(root) / "names.db").string(), {});
        assert(opened);
        (void) opened;
        index_put(store, deep / "report.csv");
        index_put(store, idx / "court" / "stale_adhoc.txt"); // indexed, gone from disk
        index_put(store, idx / ".claude" / "adhoc_copy" / "f.txt");
        index_put(store, idx / "court" / "exclude_me" / "adhoc_x.txt");

        auto indexed = [&](std::vector<std::string> roots, content::Store* s) {
            return FileProvider(FileProviderOptions{
                .roots = std::move(roots),
                .excludes = {".git", "node_modules", "exclude_me", "names.db"},
                .min_query = 2,
                .max_results = 100,
                .index = s,
                .index_roots = {idx.string()},
            });
        };

        // Root containing the indexed tree: the indexed part comes from the
        // index (walk pruned there), the rest is walked.
        auto prov = indexed({root}, &store);
        auto r = prov.query(mkq("adhoc"));
        assert(has_path(r, idx / "court" / "reports" / "adhoc"));    // folder derived from index
        assert(has_path(r, fs::path(root) / "outside_adhoc.txt"));   // walked
        assert(!has_path(r, idx / "court" / "walk_only_adhoc.txt")); // pruned from walk
        assert(!has_path(r, idx / "court" / "stale_adhoc.txt"));     // verified against disk
        assert(!has_path(r, idx / ".claude" / "adhoc_copy"));        // hidden rule applies
        assert(!has_path(r, idx / "court" / "exclude_me" / "adhoc_x.txt")); // excludes apply
        auto folder = std::ranges::find_if(r, [&](const ListItem& it) {
            return it.path == (idx / "court" / "reports" / "adhoc").string();
        });
        assert(folder != r.end() && folder->kind == ItemKind::Folder);

        // A leading '.' admits hidden index hits, like the walk.
        auto dot = prov.query(mkq(".claude"));
        assert(has_path(dot, idx / ".claude"));

        // Root inside the indexed tree: answered entirely from the index,
        // scoped to that root.
        auto inner = indexed({(idx / "court" / "reports").string()}, &store);
        auto ri = inner.query(mkq("report"));
        assert(has_path(ri, deep / "report.csv"));
        assert(has_path(ri, idx / "court" / "reports")); // top-level root matches by name

        // Index unusable (query fails) → fall back to walking everything.
        content::Store closed;
        auto fallback = indexed({root}, &closed);
        auto rf = fallback.query(mkq("adhoc"));
        assert(has_path(rf, idx / "court" / "walk_only_adhoc.txt"));
        assert(has_path(rf, idx / "court" / "reports" / "adhoc"));

        store.close();
    }

    cleanup();
    return 0;
}
