#ifndef MCGUFFIN_EXTRACTOR_H
#define MCGUFFIN_EXTRACTOR_H

#include <map>
#include <string>
#include <vector>

using namespace std;

struct extractedLink {
    string url;
    bool inNavigation = false;  // inside nav, header, footer or aside
};

struct extractionResult {
    vector<extractedLink> links;
    string baseHref;
    string metaRefreshUrl;
    string hashableBody;        // page without script and style
};

extractionResult extractLinks(string body);
string parseTagName(string piece);
map<string, string> parseAttributes(string piece);
string decodeEntities(string text);
string parseMetaRefresh(string content);

#endif //MCGUFFIN_EXTRACTOR_H