#include <string>
#include <vector>
#include "Bridges.h"
#include "GraphAdList.h"
#include "graph.h"

using namespace std;
using namespace bridges;

void createGraph(const vector<string>& vert, const vector<string>& edges) {
    GraphAdjList<string> graph;

    Bridges bridges (110, "gTayona", "964981320515");
    bridges.setTitle("Crawling Into Hell");
    bridges.setDescription("Web Crawler Project for CSCI-26");
    bridges.visualize();
}