#ifndef MCGUFFIN_GRAPH_H
#define MCGUFFIN_GRAPH_H

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Durant's scraper will provide this callback. It takes one absolute URL and
// returns the absolute URLs found on that page.
using LinkFetcher = std::function<std::vector<std::string>(const std::string&)>;

struct GraphNode {
    std::string url;
    int distance = -1; // shortest number of hops from the starting URL
    std::vector<std::string> outgoing; // directed links from this page
};

class WebsiteGraph {
public:
    // Call either traversal with a link-fetching function and the desired limits.
    void crawlBFS(const std::string& startUrl, const LinkFetcher& fetchLinks,#ifndef MCGUFFIN_GRAPH_H
#define MCGUFFIN_GRAPH_H

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Durant's scraper will provide this callback. It takes one absolute URL and
// returns the absolute URLs found on that page.
using LinkFetcher = std::function<std::vector<std::string>(const std::string&)>;

class crawler;

struct GraphNode {
    std::string url;
    int distance = -1; // shortest number of hops from the starting URL
    std::vector<std::string> outgoing; // directed links from this page
    std::vector<int> weights;   // weight of each outgoing link (filled by loadFromCrawler)
    int incoming = 0;           // total weight of links pointing here (page value)
    std::string status;         // fetched, failed or unfetched (filled by loadFromCrawler)
};

class WebsiteGraph {
public:
    // Call either traversal with a link-fetching function and the desired limits.
    void crawlBFS(const std::string& startUrl, const LinkFetcher& fetchLinks,
                  int maxDepth = 3, std::size_t maxNodes = 1000);
    void crawlDFS(const std::string& startUrl, const LinkFetcher& fetchLinks,
                  int maxDepth = 3, std::size_t maxNodes = 1000);

    // part 1 link: build the graph from a finished crawl, then traverse it with no new downloads
    void loadFromCrawler(crawler& source, bool skipNavigation = true);
    std::vector<std::string> bfs(const std::string& startUrl, int maxDepth = 3);
    std::vector<std::string> dfs(const std::string& startUrl, int maxDepth = 3);

    const std::unordered_map<std::string, GraphNode>& nodes() const { return nodes_; }
    std::size_t size() const { return nodes_.size(); }

private:
    std::unordered_map<std::string, GraphNode> nodes_;
    void reset(const std::string& startUrl, int maxDepth, std::size_t maxNodes);
    void addLinks(const std::string& source, const std::vector<std::string>& links,
                  std::size_t maxNodes);
    void updateShortestDistances(const std::string& startUrl);
};

#endif
                  int maxDepth = 3, std::size_t maxNodes = 1000);
    void crawlDFS(const std::string& startUrl, const LinkFetcher& fetchLinks,
                  int maxDepth = 3, std::size_t maxNodes = 1000);

    const std::unordered_map<std::string, GraphNode>& nodes() const { return nodes_; }
    std::size_t size() const { return nodes_.size(); }

private:
    std::unordered_map<std::string, GraphNode> nodes_;
    void reset(const std::string& startUrl, int maxDepth, std::size_t maxNodes);
    void addLinks(const std::string& source, const std::vector<std::string>& links,
                  std::size_t maxNodes);
    void updateShortestDistances(const std::string& startUrl);
};

#endif

