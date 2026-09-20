// IconResolver.hpp - Shared freedesktop icon lookup + raster cache.
//
// Merges qypr's resolver (theme-chain/inheritance, all-context search,
// data:/file: URIs, reverse-DNS fallbacks, bounded miss-caching) with
// waylaunch's spec metadata parsing (per-subdir Type/Size/MinSize/MaxSize
// with size-distance ordering) and requested-size rasterization (SVGs render
// at the target size instead of a fixed tile).
//
// Surfaces are cairo ARGB32. SVG decodes at `size`; PNGs load at native
// resolution (callers scale on paint, as before). Results — including misses
// (nullptr) — are cached under name@size, bounded, so unknown names never
// re-scan the theme chain.

#pragma once

#include <cairo/cairo.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace qypr {

class IconResolver {
public:
    IconResolver();
    ~IconResolver();

    IconResolver(const IconResolver&) = delete;
    IconResolver& operator=(const IconResolver&) = delete;

    // Look up an icon by freedesktop name, absolute path, file:// or data:
    // URI. Returns a cached surface sized ~`size` px (caller must NOT unref —
    // the cache owns it) or nullptr if not found.
    cairo_surface_t* get(const std::string& icon, int size = 48);

    // Singleton access — qypr's App owns the instance; waylaunch keeps a
    // member instead. Test suites inject fakes here.
    static IconResolver& instance();
    static void setInstance(IconResolver* resolver);

private:
    static constexpr size_t kMaxCached = 128;  // name@size entries, misses included

    cairo_surface_t* loadPng(const std::string& path);
    cairo_surface_t* loadSvg(const std::string& path, int size);
    cairo_surface_t* loadFile(const std::string& path, int size);  // dispatch by extension
    cairo_surface_t* loadDataUri(const std::string& uri);
    cairo_surface_t* resolveName(const std::string& name, int size);
    std::string findFile(const std::string& name);

    // Freedesktop icon-theme lookup: active theme + its inheritance chain,
    // searching every context subdir ordered by size-distance to `size`
    // (scalable first). This resolves tray status/device icons, not just apps.
    cairo_surface_t* lookupThemed(const std::string& name, int size);
    const std::vector<std::string>& themeChain();
    struct Subdir {
        std::string name;
        std::string type;  // "Scalable", "Fixed", "Threshold" (or "" when unknown)
        int size = 0;
        int minSize = 0;
        int maxSize = 0;
    };
    // Parsed index.theme Directories= entries for one theme dir (cached).
    const std::vector<Subdir>& themeSubdirs(const std::string& themeDir);
    static std::vector<Subdir> parseThemeDirs(const std::string& indexPath);

    std::unordered_map<std::string, cairo_surface_t*> cache_;
    std::vector<std::string> themeChain_;
    std::vector<std::string> baseDirs_;
    bool themeChainBuilt_ = false;
    std::unordered_map<std::string, std::vector<Subdir>> themeSubdirsCache_;
    static IconResolver* instance_;
};

}  // namespace qypr
