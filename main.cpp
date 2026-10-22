#include <exception>
#include <iostream>
#include "crawler.h"
#include "read.h"

using namespace std;

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