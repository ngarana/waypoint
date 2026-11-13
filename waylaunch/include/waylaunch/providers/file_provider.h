#pragma once

#include "waylaunch/providers/result_provider.h"
#include <string>
#include <vector>

namespace waylaunch {

class HistoryStore;
namespace content {
class Store;
}

struct FileProviderOptions {
    std::vector<std::string> roots; // absolute dirs to search
    // Skipped names: an exact basename or a glob (*.tmp) matches any entry; a
    // pattern containing '/' (go/pkg, .local/share/Trash) matches the trailing
    // path components of an entry.
    std::vector<std::string> excludes;
    int min_query = 2;
    int max_results = 6;
    // Dot-files and dot-folders are skipped (as `fd` does) unless this is set
    // or the query itself starts with '.'.
    bool include_hidden = false;
    const HistoryStore* history = nullptr;
    // Read-only waylaunchd index and the trees it covers. Roots inside an
    // indexed tree are answered from the index; indexed trees inside a root are
    // answered from the index and pruned from the walk. Null → walk only.
    content::Store* index = nullptr;
    std::vector<std::string> index_roots;
};

// Filename/foldername search, ranked by name match, path depth, recency and
// frecency. Indexed trees are served by the waylaunchd index (complete and
// instant); everything else by a bounded native filesystem walk, which is also
// the fallback whenever the index is missing or a query against it fails.
// Async (runs on the search worker). Register only when [search].files is on.
class FileProvider : public ResultProvider {
  public:
    explicit FileProvider(FileProviderOptions opts);

    std::string id() const override { return "files"; }
    bool is_async() const override { return true; }
    bool is_available() const override;
    std::vector<ListItem> query(const ProviderQuery&) override;
    bool activate(const ListItem&) override;

  private:
    FileProviderOptions opts_;
};

} // namespace waylaunch
