#include <iostream>
#include "crawler.h"
#include "dnsResolver.h"
#include "read.h"
#include "url.h"
#include "urlStandard.h"
#include <string>
#include "Bridges.h"
#include "GraphAdjList.h"

using namespace std;
using namespace bridges;

int main() {
    //std::cout << "Hello, World!" << std::endl;
    Bridges bridges (12, "gTayona", "964981320515");
	bridges.setTitle("The Crawling of Webs");
	bridges.setDescription("Web Crawler Project for CSCI-26");

	GraphAdjList<string> graph;

	vector<string> temp = {"a","b","c"};

	for (unsigned int i = 0; i < temp.size(); i++) {
		string tempura = temp.at(i);
		graph.addVertex(tempura);
	}

	graph.getVertex(temp.at(0))->setColor("red");
	

	bridges.setDataStructure(graph);
	bridges.visualize();

	return 0;
}
