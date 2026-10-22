#include <algorithm>
#include <exception>
#include <iostream>
#include "crawler.h"
#include "graph.h"
#include "read.h"

using namespace std;

// part 2: bfs and dfs over the finished crawl, plus the most-linked pages
void runGraph(crawler& webCrawler, string seedUrl) {
    WebsiteGraph graph;
    graph.loadFromCrawler(webCrawler);
    string start = webCrawler.getPages().at(webCrawler.lookupId(seedUrl)).fetchUrl;
    vector<string> bfsOrder = graph.bfs(start);
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
