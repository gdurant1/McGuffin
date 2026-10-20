#include "graph.h"
#include <cassert>
#include <iostream>
#include <unordered_map>

int main() {
    const std::unordered_map<std::string, std::vector<std::string>> testLinks = {
        {"https://example.com/", {"https://example.com/a", "https://example.com/b", "https://example.com/a#section"}},
        {"https://example.com/a", {"https://example.com/c", "https://example.com/"}},
        {"https://example.com/b", {"https://example.com/c"}},
        {"https://example.com/c", {"https://example.com/d"}}
    };
    auto fakeScraper = [&](const std::string& page) -> std::vector<std::string> {
        auto it = testLinks.find(page);
        return it == testLinks.end() ? std::vector<std::string>{} : it->second;
    };

    WebsiteGraph graph;
    for (bool bfs : {true, false}) {
        if (bfs) graph.crawlBFS("https://example.com/", fakeScraper, 3, 1000);
        else graph.crawlDFS("https://example.com/", fakeScraper, 3, 1000);
        assert(graph.size() == 5);
        assert(graph.nodes().at("https://example.com/").distance == 0);
        assert(graph.nodes().at("https://example.com/a").distance == 1);
        assert(graph.nodes().at("https://example.com/c").distance == 2);
        assert(graph.nodes().at("https://example.com/d").distance == 3);
        assert(graph.nodes().at("https://example.com/a").outgoing.size() == 2);
        std::cout << (bfs ? "BFS" : "DFS") << " passed: " << graph.size() << " nodes\n";
    }
    graph.crawlBFS("https://example.com/", fakeScraper, 1, 2);
    assert(graph.size() == 2);
    std::cout << "Depth and size limits passed\n";
}

