// IconResolver.cpp - See the header. Lookup order per name:
// active-theme chain (with Inherits) × base dirs × size-ordered subdirs,
// then the legacy hicolor/Adwaita/pixmaps fallback, then reverse-DNS and
// lowercase rewrites of dotted names.
#include "render/IconResolver.hpp"

#include <librsvg/rsvg.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace qypr {

namespace {

bool fileExists(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "r");
    if (f != nullptr) {
        std::fclose(f);
        return true;
    }
    return false;
}

std::string trimStr(const std::string& in) {
    size_t a = 0;
    size_t b = in.size();
    while (a < b && std::isspace(static_cast<unsigned char>(in[a])) != 0) { ++a; }
    while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1])) != 0) { --b; }
    return in.substr(a, b - a);
}

// Split a comma-separated list, trimming each element.
std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t nx = s.find(',', pos);
        std::string part =
            trimStr(s.substr(pos, nx == std::string::npos ? std::string::npos : nx - pos));
        if (!part.empty()) { out.push_back(part); }
        if (nx == std::string::npos) { break; }
        pos = nx + 1;
    }
    return out;
}

// First "key=value" match in an INI-ish file; trimmed value, "" if absent.
std::string iniValue(const std::string& path, const char* key) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) { return {}; }
    char line[2048];
    std::string result;
    while (std::fgets(line, sizeof line, f) != nullptr) {
        std::string l(line);
        auto eq = l.find('=');
        if (eq == std::string::npos) { continue; }
        if (trimStr(l.substr(0, eq)) == key) {
            result = trimStr(l.substr(eq + 1));
            break;
        }
    }
    std::fclose(f);
    return result;
}

// The configured icon theme, best-effort and without spawning a process.
// Explicit overrides first (waylaunch honors WAYLAUNCH_ICON_THEME, then a
// GTK_ICON_THEME setting without needing GTK itself), then GTK settings.ini
// (the reliable file source), then XDG_ICON_THEME, else "hicolor".
// Reads startup environment only.
std::string activeIconTheme() {
    if (const char* t = std::getenv("WAYLAUNCH_ICON_THEME"); t != nullptr && *t != '\0') {
        return t;
    }
    if (const char* t = std::getenv("GTK_ICON_THEME"); t != nullptr && *t != '\0') {
        return t;
    }
    const char* home = std::getenv("HOME");
    std::string h = home != nullptr ? home : "";
    if (!h.empty()) {
        for (const std::string& p :
             {h + "/.config/gtk-4.0/settings.ini", h + "/.config/gtk-3.0/settings.ini"}) {
            std::string v = iniValue(p, "gtk-icon-theme-name");
            if (!v.empty()) { return v; }
        }
    }
    if (const char* e = std::getenv("XDG_ICON_THEME")) {
        if (*e != '\0') { return e; }
    }
    return "hicolor";
}

// Freedesktop icon base directories in search order.
std::vector<std::string> iconBaseDirs() {
    std::vector<std::string> dirs;
    const char* home = std::getenv("HOME");
    if (home != nullptr) { dirs.push_back(std::string(home) + "/.icons"); }
    if (const char* xdgData = std::getenv("XDG_DATA_HOME");
        xdgData != nullptr && *xdgData != '\0') {
        dirs.push_back(std::string(xdgData) + "/icons");
    } else if (home != nullptr) {
        dirs.push_back(std::string(home) + "/.local/share/icons");
    }
    const char* rawDirs = std::getenv("XDG_DATA_DIRS");
    std::string dataDirs =
        rawDirs != nullptr ? rawDirs : std::string("/usr/local/share:/usr/share");
    size_t pos = 0;
    while (pos <= dataDirs.size()) {
        size_t nx = dataDirs.find(':', pos);
        std::string d =
            dataDirs.substr(pos, nx == std::string::npos ? std::string::npos : nx - pos);
        if (!d.empty()) { dirs.push_back(d + "/icons"); }
        if (nx == std::string::npos) { break; }
        pos = nx + 1;
    }
    return dirs;
}

}  // namespace

IconResolver* IconResolver::instance_ = nullptr;

IconResolver::IconResolver() = default;

IconResolver::~IconResolver() {
    for (auto& [_, s] : cache_) { cairo_surface_destroy(s); }
}

IconResolver& IconResolver::instance() {
    if (!instance_) {
        static IconResolver r;
        instance_ = &r;
    }
    return *instance_;
}

void IconResolver::setInstance(IconResolver* resolver) {
    instance_ = resolver;
}

// ---------------------------------------------------------------------------
// PNG loading
// ---------------------------------------------------------------------------

cairo_surface_t* IconResolver::loadPng(const std::string& path) {
    cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return nullptr;
    }
    return s;
}

// ---------------------------------------------------------------------------
// SVG loading (librsvg → cairo surface rasterized at the requested size)
// ---------------------------------------------------------------------------

cairo_surface_t* IconResolver::loadSvg(const std::string& path, int size) {
    GError* err = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
    if (!handle) {
        if (err) { g_error_free(err); }
        return nullptr;
    }

    const int px = size > 0 ? size : 48;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        g_object_unref(handle);
        return nullptr;
    }

    double w = px;
    double h = px;
    double iw = 0;
    double ih = 0;
    if (rsvg_handle_get_intrinsic_size_in_pixels(handle, &iw, &ih) && iw > 0 && ih > 0) {
        double scale = std::min(px / iw, px / ih);
        w = iw * scale;
        h = ih * scale;
    }

    cairo_t* cr = cairo_create(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    RsvgRectangle viewport{(px - w) / 2.0, (px - h) / 2.0, w, h};
    gboolean ok = rsvg_handle_render_document(handle, cr, &viewport, &err);
    cairo_destroy(cr);
    g_object_unref(handle);

    if (!ok) {
        if (err) { g_error_free(err); }
        cairo_surface_destroy(surf);
        return nullptr;
    }
    cairo_surface_flush(surf);
    return surf;
}

// Pick the decoder from the file extension.
cairo_surface_t* IconResolver::loadFile(const std::string& path, int size) {
    auto endsWith = [&](const char* ext) {
        size_t n = std::strlen(ext);
        return path.size() >= n && std::equal(path.end() - n, path.end(), ext, [](char a, char b) {
                   return std::tolower((unsigned char)a) == b;
               });
    };
    if (endsWith(".svg") || endsWith(".svgz")) { return loadSvg(path, size); }
    return loadPng(path);
}

// ---------------------------------------------------------------------------
// data: URI decoding (base64 PNG)
// ---------------------------------------------------------------------------

namespace {

int b64val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') { return c - 'A'; }
    if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
    if (c >= '0' && c <= '9') { return c - '0' + 52; }
    if (c == '+') { return 62; }
    if (c == '/') { return 63; }
    return -1;
}

std::string base64Decode(const char* src, size_t len) {
    std::string out;
    out.reserve(len * 3 / 4);
    unsigned int buf = 0;
    int bits = 0;
    for (size_t i = 0; i < len; ++i) {
        int v = b64val(static_cast<unsigned char>(src[i]));
        if (v < 0) { continue; }
        buf = (buf << 6) | static_cast<unsigned int>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

struct PngReader {
    const char* ptr;
    size_t rem;
};

cairo_status_t pngRead(void* closure, unsigned char* data, unsigned int length) {
    auto* r = static_cast<PngReader*>(closure);
    if (r->rem < length) { return CAIRO_STATUS_READ_ERROR; }
    std::memcpy(data, r->ptr, length);
    r->ptr += length;
    r->rem -= length;
    return CAIRO_STATUS_SUCCESS;
}

}  // namespace

cairo_surface_t* IconResolver::loadDataUri(const std::string& uri) {
    const char* b64 = std::strstr(uri.c_str(), "base64,");
    if (!b64) { return nullptr; }
    b64 += 7;
    std::string raw = base64Decode(b64, std::strlen(b64));
    if (raw.size() < 8) { return nullptr; }

    // Check PNG magic bytes.
    if (static_cast<unsigned char>(raw[0]) != 0x89 || raw[1] != 'P' || raw[2] != 'N' ||
        raw[3] != 'G') {
        return nullptr;
    }

    PngReader r{raw.data(), raw.size()};
    cairo_surface_t* s = cairo_image_surface_create_from_png_stream(pngRead, &r);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return nullptr;
    }
    return s;
}

// ---------------------------------------------------------------------------
// XDG icon theme lookup
// ---------------------------------------------------------------------------

std::string IconResolver::findFile(const std::string& name) {
    static const char* kDirs[] = {
        "/usr/share/icons/hicolor",
        "/usr/share/icons/Adwaita",
        "/usr/share/pixmaps",
        nullptr,
    };

    std::vector<std::string> xdgDirs;
    if (const char* dataDirs = std::getenv("XDG_DATA_DIRS")) {
        std::string dirs(dataDirs);
        size_t pos = 0;
        while (pos < dirs.size()) {
            size_t next = dirs.find(':', pos);
            std::string d =
                (next != std::string::npos) ? dirs.substr(pos, next - pos) : dirs.substr(pos);
            xdgDirs.push_back(d + "/icons/hicolor");
            pos = (next != std::string::npos) ? next + 1 : dirs.size();
        }
    }

    // Legacy fixed-size sweep for the fallback dirs (tile-sized rasters
    // first); the metadata-driven themed lookup above already handled
    // size-distance ordering for real themes.
    static const char* kSizes[] = {"48x48",   "64x64", "32x32", "96x96",
                                   "128x128", "24x24", "22x22", "16x16"};

    auto tryDir = [&](const std::string& base) -> std::string {
        for (const char* sz : kSizes) {
            std::string p = base + "/" + sz + "/apps/" + name + ".png";
            if (fileExists(p)) { return p; }
        }
        std::string svg = base + "/scalable/apps/" + name + ".svg";
        if (fileExists(svg)) { return svg; }
        std::string png = base + "/" + name + ".png";
        if (fileExists(png)) { return png; }
        std::string flatSvg = base + "/" + name + ".svg";
        if (fileExists(flatSvg)) { return flatSvg; }
        return {};
    };

    for (const char** d = kDirs; *d != nullptr; ++d) {
        std::string r = tryDir(*d);
        if (!r.empty()) { return r; }
    }
    for (const auto& d : xdgDirs) {
        std::string r = tryDir(d);
        if (!r.empty()) { return r; }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Name resolution
// ---------------------------------------------------------------------------

// The active theme followed by its Inherits chain (breadth-first, de-duped),
// always ending with hicolor. Built once.
const std::vector<std::string>& IconResolver::themeChain() {
    if (themeChainBuilt_) { return themeChain_; }
    themeChainBuilt_ = true;
    baseDirs_ = iconBaseDirs();

    std::vector<std::string> queue{activeIconTheme()};
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < queue.size(); ++i) {
        const std::string t = queue[i];
        if (t.empty() || seen.count(t) != 0) { continue; }
        seen.insert(t);
        themeChain_.push_back(t);
        // Pull Inherits from the first base dir that carries this theme.
        for (const auto& base : baseDirs_) {
            std::string inh = iniValue(base + "/" + t + "/index.theme", "Inherits");
            if (!inh.empty()) {
                for (auto& parent : splitCsv(inh)) { queue.push_back(parent); }
                break;
            }
        }
    }
    if (seen.count("hicolor") == 0) { themeChain_.push_back("hicolor"); }
    return themeChain_;
}

// Parse one index.theme's Directories= entries with their spec metadata
// (Type/Size/MinSize/MaxSize); malformed entries degrade to Fixed/0 rather
// than dropping the icon.
std::vector<IconResolver::Subdir> IconResolver::parseThemeDirs(const std::string& indexPath) {
    std::vector<IconResolver::Subdir> out;
    FILE* f = std::fopen(indexPath.c_str(), "r");
    if (!f) { return out; }
    char line[2048];
    std::string section;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sections;
    while (std::fgets(line, sizeof line, f) != nullptr) {
        std::string l = trimStr(line);
        if (l.empty() || l[0] == '#' || l[0] == ';') { continue; }
        if (l.front() == '[' && l.back() == ']') {
            section = l.substr(1, l.size() - 2);
            continue;
        }
        auto eq = l.find('=');
        if (eq == std::string::npos || section.empty()) { continue; }
        sections[section][trimStr(l.substr(0, eq))] = trimStr(l.substr(eq + 1));
    }
    std::fclose(f);

    auto toInt = [](const std::string& v) {
        try {
            return std::stoi(v);
        } catch (const std::exception&) { return 0; }
    };
    auto dirsIt = sections.find("Icon Theme");
    if (dirsIt == sections.end()) { return out; }
    for (const auto& name : splitCsv(dirsIt->second["Directories"])) {
        IconResolver::Subdir d;
        d.name = name;
        auto it = sections.find(name);
        if (it != sections.end()) {
            const auto& kv = it->second;
            auto get = [&](const char* k) {
                auto f = kv.find(k);
                return f != kv.end() ? f->second : std::string{};
            };
            d.type = get("Type");
            d.size = toInt(get("Size"));
            d.minSize = toInt(get("MinSize"));
            d.maxSize = toInt(get("MaxSize"));
        }
        out.push_back(std::move(d));
    }
    return out;
}

// A theme directory's parsed index.theme Directories= entries (spec metadata).
// Ordering by requested size happens in lookupThemed (size varies per call);
// this cache holds the parse. Themes without index.theme yield no entries,
// and the legacy findFile() fallback below covers them.
const std::vector<IconResolver::Subdir>& IconResolver::themeSubdirs(const std::string& themeDir) {
    auto it = themeSubdirsCache_.find(themeDir);
    if (it != themeSubdirsCache_.end()) { return it->second; }
    auto& slot = themeSubdirsCache_[themeDir];
    slot = parseThemeDirs(themeDir + "/index.theme");
    return slot;
}

// Full freedesktop lookup across the theme chain and every context subdir,
// nearest to `size`. Vector (.svg/.svgz) sources win ties: they rasterize
// exactly at the requested size, while a PNG can only be scaled.
cairo_surface_t* IconResolver::lookupThemed(const std::string& name, int size) {
    struct Hit {
        std::string path;
        int distance;
        bool scalable;
    };
    std::vector<Hit> hits;
    auto distance = [size](const Subdir& d) {
        if (d.type == "Scalable") {
            if (d.minSize > 0 && size < d.minSize) { return d.minSize - size; }
            if (d.maxSize > 0 && size > d.maxSize) { return size - d.maxSize; }
            return 0;
        }
        if (d.type == "Threshold" || d.type.empty()) {
            // Threshold without metadata behaves like Fixed at Size.
            return std::abs(d.size - size);
        }
        return std::abs(d.size - size);
    };
    for (const auto& theme : themeChain()) {
        for (const auto& base : baseDirs_) {
            std::string themeDir = base + "/" + theme;
            for (const auto& sub : themeSubdirs(themeDir)) {
                int dist = distance(sub);
                for (const char* ext : {".svg", ".svgz", ".png"}) {
                    std::string p = themeDir + "/" + sub.name + "/" + name + ext;
                    if (fileExists(p)) {
                        hits.push_back({p, dist, ext[1] == 's'});
                    }
                }
            }
        }
        if (!hits.empty()) { break; }  // theme priority beats size precision
    }
    if (hits.empty()) { return nullptr; }
    std::ranges::sort(hits, [](const Hit& a, const Hit& b) {
        if (a.scalable != b.scalable) { return a.scalable; }
        return a.distance < b.distance;
    });
    return loadFile(hits.front().path, size);
}

cairo_surface_t* IconResolver::resolveName(const std::string& name, int size) {
    // Proper themed lookup first (active theme + inheritance + all contexts):
    // this is what resolves tray status/device icons, not just app icons.
    if (cairo_surface_t* s = lookupThemed(name, size)) { return s; }

    std::string path = findFile(name);
    if (!path.empty()) { return loadFile(path, size); }

    if (auto pos = name.rfind('.'); pos != std::string::npos && pos > 0) {
        std::string simple = name.substr(pos + 1);
        path = findFile(simple);
        if (!path.empty()) { return loadFile(path, size); }

        // Reverse-DNS app IDs (e.g. "org.chromium.Chromium") commonly map to a
        // lowercase icon file ("chromium.png"); try the last component lowered.
        std::string simpleLower = simple;
        std::transform(simpleLower.begin(), simpleLower.end(), simpleLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (simpleLower != simple) {
            path = findFile(simpleLower);
            if (!path.empty()) { return loadFile(path, size); }
        }

        auto dot2 = name.find('.');
        if (dot2 != std::string::npos && dot2 < pos) {
            std::string kde = name.substr(dot2 + 1);
            std::transform(kde.begin(), kde.end(), kde.begin(),
                           [](unsigned char c) { return c == '.' ? '-' : c; });
            path = findFile(kde);
            if (!path.empty()) { return loadFile(path, size); }
        }
    }

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return c == '.' ? '-' : std::tolower(c); });
    path = findFile(lower);
    if (!path.empty()) { return loadFile(path, size); }

    return nullptr;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

cairo_surface_t* IconResolver::get(const std::string& icon, int size) {
    if (icon.empty()) { return nullptr; }
    const int px = size > 0 ? size : 48;
    const std::string key = icon + "@" + std::to_string(px);
    if (auto it = cache_.find(key); it != cache_.end()) { return it->second; }

    cairo_surface_t* s = nullptr;

    if (icon.rfind("data:", 0) == 0) {
        s = loadDataUri(icon);
    } else if (icon.rfind("file://", 0) == 0) {
        s = loadFile(icon.substr(7), px);
    } else if (!icon.empty() && icon[0] == '/') {
        // The freedesktop app_icon may be a full path rather than a themed
        // name (e.g. "/usr/lib/kitty/logo/kitty.png"). Load it directly;
        // resolveName() would only look it up as a theme name and miss it.
        s = loadFile(icon, px);
    } else {
        s = resolveName(icon, px);
    }

    // Cache the result — including a miss (nullptr) — so a name that does not
    // resolve is not re-scanned across the whole theme chain on every call
    // (the resolve path now walks many contexts). A given item's icon name is
    // stable until it signals a new name, which is a different cache key.
    // Evict an arbitrary entry past the bound (unordered_map has no LRU
    // order; the bound, not the policy, is what matters here).
    if (cache_.size() >= kMaxCached) {
        auto it = cache_.begin();
        if (it->second) { cairo_surface_destroy(it->second); }  // no-op on nullptr
        cache_.erase(it);
    }
    cache_[key] = s;
    return s;
}

}  // namespace qypr
