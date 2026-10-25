#ifndef MCGUFFIN_BRIDGES_H
#define MCGUFFIN_BRIDGES_H

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <boost/json.hpp>
#include "Bridges.h"
#include "GraphAdjList.h"
#include "graph.h"

using namespace std;
using namespace bridges;
namespace json = boost::json;

inline void createGraph(const string& graphPath) {

	ifstream in(graphPath);
	if (!in) {
		cerr << "error: can't read " << graphPath << endl;
		return;
	}
	stringstream text;
	text << in.rdbuf();
	json::value root;
	try {
		root = json::parse(text.str());
	}
	catch (const exception& error) {
		cerr << "error: bad json in " << graphPath << ": " << error.what() << endl;
		return;
	}
	const json::object& data = root.as_object();
	string seed = json::value_to<string>(data.at("seed"));
	const json::array& nodes = data.at("nodes").as_array();
	const json::array& edges = data.at("edges").as_array();

	Bridges bridges (110, "gTayona", "964981320515");
	bridges.setTitle("Crawling Into Hell");
	bridges.setDescription("Web Crawler Project for CSCI-26");

	GraphAdjList<string, int, int> graph;

	for (unsigned int i = 0; i < nodes.size(); i++) {
		const json::object& node = nodes.at(i).as_object();
		string url = json::value_to<string>(node.at("url"));
		int dist = json::value_to<int>(node.at("distance"));

		graph.addVertex(url, dist);
		if (url == seed) {
			graph.getVertex(url)->setLabel("Seed URL");
		} else {
			graph.getVertex(url)->setLabel(url);
		}
		if (dist == 0) {
			graph.getVertex(url)->setColor("red");
		} else if (dist == 1) {
			graph.getVertex(url)->setColor("orange");
		} else if (dist == 2) {
			graph.getVertex(url)->setColor("yellow");
		} else if (dist == 3) {
			graph.getVertex(url)->setColor("green");
		} else {
			graph.getVertex(url)->setColor("blue");
		}
	}

	for (unsigned int i = 0; i < edges.size(); i++) {
		const json::object& edge = edges.at(i).as_object();
		string from = json::value_to<string>(edge.at("from"));
		string to = json::value_to<string>(edge.at("to"));
		int weight = json::value_to<int>(edge.at("weight"));
		graph.addEdge(from, to, weight);
	}

	bridges.setDataStructure(graph);
	bridges.visualize();
}

#endif
