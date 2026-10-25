#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <boost/json.hpp>
#include "crawler.h"
#include "graph.h"
#include "read.h"
#include "bridges.h"
#include <Bridges.h>

using namespace std;
namespace json = boost::json;

bool saveGraphJson(const WebsiteGraph& graph, string start, const vector<string>& bfsOrder, string graphPath) {
    unordered_set<string> reached;
    for (const string& url : bfsOrder) {
        if (graph.nodes().at(url).status == "fetched") {
            reached.insert(url);
        }
    }
    json::array nodeList;
    json::array edgeList;
    for (const string& url : bfsOrder) {
        if (!reached.contains(url)) {
            continue;
        }
        const GraphNode& node = graph.nodes().at(url);
        nodeList.push_back({{"url", node.url}, {"distance", node.distance}});
        for (size_t i = 0; i < node.outgoing.size(); i++) {
            if (reached.contains(node.outgoing[i])) {
                edgeList.push_back({{"from", node.url}, {"to", node.outgoing[i]}, {"weight", node.weights[i]}});
            }
        }
    }
    json::object root;
    root["seed"] = start;
    root["nodes"] = nodeList;
    root["edges"] = edgeList;

    ofstream out(graphPath);
    if (!out) {
        cerr << "error: can't write " << graphPath << endl;
        return false;
    }
    out << json::serialize(root);
    cout << "graph saved in " << graphPath << endl;
    return true;
}

// part 2: bfs and dfs over the finished crawl, plus the most-linked pages
void runGraph(crawler& webCrawler, string seedUrl) {
    WebsiteGraph graph;
    graph.loadFromCrawler(webCrawler);
    string start = webCrawler.getPages().at(webCrawler.lookupId(seedUrl)).fetchUrl;
    vector<string> bfsOrder = graph.bfs(start);

    string graphPath = webCrawler.getDataPath();
    graphPath = graphPath.substr(0, graphPath.size() - 5) + "_graph.json";
    bool graphSaved = saveGraphJson(graph, start, bfsOrder, graphPath);

    vector<string> dfsOrder = graph.dfs(start);
    cout << "bfs reached " << bfsOrder.size() << " pages, dfs reached " << dfsOrder.size() << " pages (depth 3)" << endl;

    vector<const GraphNode*> ranked;
    for (const auto& entry : graph.nodes()) {
        ranked.push_back(&entry.second);
    }
    sort(ranked.begin(), ranked.end(), [](const GraphNode* a, const GraphNode* b) { return a->incoming > b->incoming; });
    cout << "most-linked pages:" << endl;
    for (int i = 0; i < (int)ranked.size() && i < 10; i++) {
        cout << "  " << ranked[i]->incoming << "  " << ranked[i]->url << " (distance " << ranked[i]->distance << ")" << endl;
    }

    if (graphSaved) {
        createGraph(graphPath);
    }
}

// crawl one seed, print every url found; returns 0 on success, 1 on error
int runCrawler(string seedUrl, string outputFolder) {
    crawlConfig config;
    config.seedUrl = seedUrl;
    config.outputFolder = outputFolder;
    try {
        crawler webCrawler(config);
        vector<string> urls = webCrawler.run();
        for (const string& found : urls) {
            cout << found << endl;
        }
        cout << urls.size() << " urls found, saved in " << outputFolder << "/" << endl;
        runGraph(webCrawler, seedUrl);
    }
    catch (const exception& error) {
        cerr << "error: " << error.what() << endl;
        return 1;
    }
    return 0;
}

// usage: McGuffin [seedUrl] [outputFolder]; asks for the seed when none is given
int main(int argc, char* argv[]) {
    
    string seedUrl;
    string outputFolder = "data";
    if (argc > 1) {
        seedUrl = argv[1];
    }
    else {
        seedUrl = readline("seed url: ");
    }
    if (argc > 2) {
        outputFolder = argv[2];
    }
    return runCrawler(seedUrl, outputFolder);




}
