#include "graph.h"

#include <queue>
#include <stack>
#include <stdexcept>
#include <unordered_set>
#include <unistd.h> // usleep, available on the school's Linux server

namespace {
std::string withoutFragment(const std::string& url) {
    return url.substr(0, url.find('#'));
}

// Delays happen BETWEEN actual requests, not before the first request.
std::vector<std::string> fetchWithDelay(const LinkFetcher& fetchLinks,
                                        const std::string& url, bool& requested) {
    if (requested) usleep(100000); // 0.1 seconds
    requested = true;
    return fetchLinks(url);
}
}

void WebsiteGraph::reset(const std::string& startUrl, int maxDepth,
                         std::size_t maxNodes) {
    if (startUrl.empty() || maxDepth < 0 || maxNodes == 0)
        throw std::invalid_argument("Start URL must be nonempty, depth >= 0, nodes >= 1");
    nodes_.clear();
    std::string root = withoutFragment(startUrl);
    nodes_.emplace(root, GraphNode{root, 0, {}});
}

void WebsiteGraph::addLinks(const std::string& source,
                            const std::vector<std::string>& links,
                            std::size_t maxNodes) {
    std::unordered_set<std::string> unique;
    for (const auto& rawLink : links) {
        std::string link = withoutFragment(rawLink);
        // Contract: scraper supplies absolute HTTP(S) URLs, not relative links.
        if (link.empty() ||
            (link.rfind("https://", 0) != 0 && link.rfind("http://", 0) != 0))
            continue;
        if (!unique.insert(link).second) continue;
        if (!nodes_.contains(link)) {
            if (nodes_.size() == maxNodes) continue;
            nodes_.emplace(link, GraphNode{link, -1, {}});
        }
        nodes_.at(source).outgoing.push_back(link);
    }
}

void WebsiteGraph::updateShortestDistances(const std::string& startUrl) {
    for (auto& [name, node] : nodes_) node.distance = -1;
    const std::string root = withoutFragment(startUrl);
    nodes_.at(root).distance = 0;
    std::queue<std::string> q;
    q.push(root);
    while (!q.empty()) {
        auto current = q.front(); q.pop();
        int nextDistance = nodes_.at(current).distance + 1;
        for (const auto& neighbor : nodes_.at(current).outgoing) {
            if (nodes_.at(neighbor).distance == -1) {
                nodes_.at(neighbor).distance = nextDistance;
                q.push(neighbor);
            }
        }
    }
}

void WebsiteGraph::crawlBFS(const std::string& startUrl, const LinkFetcher& fetchLinks,
                            int maxDepth, std::size_t maxNodes) {
    reset(startUrl, maxDepth, maxNodes);
    std::queue<std::pair<std::string, int>> pending;
    std::unordered_set<std::string> queued;
    const std::string root = withoutFragment(startUrl);
    pending.push({root, 0});
    queued.insert(root);
    bool requested = false;

    while (!pending.empty()) {
        auto [current, depth] = pending.front(); pending.pop();
        if (depth >= maxDepth) continue;
        try {
            addLinks(current, fetchWithDelay(fetchLinks, current, requested), maxNodes);
        } catch (const std::exception&) {
            continue; // Unreachable sites do not terminate traversal.
        }
        for (const auto& neighbor : nodes_.at(current).outgoing) {
            if (queued.insert(neighbor).second) pending.push({neighbor, depth + 1});
        }
    }
    updateShortestDistances(root);
}

void WebsiteGraph::crawlDFS(const std::string& startUrl, const LinkFetcher& fetchLinks,
                            int maxDepth, std::size_t maxNodes) {
    reset(startUrl, maxDepth, maxNodes);
    std::stack<std::pair<std::string, int>> pending;
    std::unordered_set<std::string> visited;
    const std::string root = withoutFragment(startUrl);
    pending.push({root, 0});
    bool requested = false;

    while (!pending.empty()) {
        auto [current, depth] = pending.top(); pending.pop();
        if (!visited.insert(current).second || depth >= maxDepth) continue;
        try {
            addLinks(current, fetchWithDelay(fetchLinks, current, requested), maxNodes);
        } catch (const std::exception&) {
            continue;
        }
        // Reverse order to explore the first listed link first.
        const auto& outgoing = nodes_.at(current).outgoing;
        for (auto it = outgoing.rbegin(); it != outgoing.rend(); ++it) {
            if (!visited.contains(*it)) pending.push({*it, depth + 1});
        }
    }
    updateShortestDistances(root);
}

