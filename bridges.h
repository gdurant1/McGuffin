#include <string>
#include <vector>
#include <Bridges.h>
#include <GraphAdList.h>
#include "graph.h"

using namespace std;
using namespace bridges;

void createGraph(const vector<string>& urls, const vector<int>& dist) {

    Bridges bridges (110, "gTayona", "964981320515");
    bridges.setTitle("Crawling Into Hell");
    bridges.setDescription("Web Crawler Project for CSCI-26");

    GraphAdjList<string, int> graph;

    for (size_t i = 0; i < urls.size(); i++) {
        graph.addNode(urls.at(i), dist.at(i));
        graph.getNode(urls.at(i))->setLabel("Seed URL");
        if (dist.at(i) = 0) {
            graph.getNode(urls.at(i))->setColor("Red");
        } else if (dist.at(i) = 1) {
            graph.getNode(urls.at(i))->setColor("Orange");
        } else if (dist.at(i) = 2) {
            graph.getNode(urls.at(i))->setColor("Yellow");
        } else if (dist.at(i) = 3) {
            graph.getNode(urls.at(i))->setColor("Green");
        } else {
            graph.getNode(urls.at(i))->setColor("Blue");
        }
    } 



    bridges.visualize();
}