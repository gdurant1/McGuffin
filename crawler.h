#ifndef MCGUFFIN_CRAWLER_H
#define MCGUFFIN_CRAWLER_H

#include <chrono>
#include <deque>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <boost/asio.hpp>

#include "extractor.h"
#include "fetch.h"
#include "urlStandard.h"

using namespace std;

struct crawlConfig {
    string seedUrl;
    int pageCap = 1000;
    int globalLimit = 8;                    // requests in flight at once
    int siteLimit = 2;                      // requests in flight per site
    int siteDelayMs = 250;                  // wait between requests to one site
    int timeoutSeconds = 10;
    int maxRedirects = 5;
    int maxBodyBytes = 5 * 1024 * 1024;
    int minHashBytes = 5 * 1024;            // smaller bodies are never merged by content
    string outputFolder = "data";
};

enum class pageStatus { queued, fetched, failed, unfetched, tooLarge, nonHtml };

struct aliasRecord {
    string key;
    string mergeType;   // how it was merged
};

struct pageRecord {
    int id = -1;
    string key;
    string fetchUrl;
    vector<string> originalUrls;
    vector<aliasRecord> aliases;
    int hostId = -1;
    int depth = 0;
    pageStatus status = pageStatus::queued;
    int httpStatus = 0;
    string contentType;
    string contentHash;
    bool certificateOk = true;
    string certificateError;
    string error;
    int retryCount = 0;
    int rateLimitCount = 0;
    bool inProgress = false;
    string lastFetched;
    int mergedInto = -1;    // set when merged into another page
};

struct edgeRecord {
    int from = -1;
    int to = -1;
    int weight = 0;                     // number of links from "from" to "to"
    bool navigationByLocation = false;  // every link was in nav, header, footer or aside
};

struct hostRecord {
    int id = -1;
    string name;
    string site;
    vector<string> ipv4;
    vector<string> ipv6;
    string canonicalName;
    bool noIpv6 = false;
};

struct siteRecord {
    string domain;
    vector<int> hostIds;
    int inFlight = 0;
    chrono::steady_clock::time_point lastRequest;
    chrono::steady_clock::time_point backoffUntil;
    int backoffMs = 0;
};

class crawler {
private:
    crawlConfig config;
    boost::asio::io_context context;
    dnsResolver resolver;
    fetch pageFetcher;
    boost::asio::steady_timer wakeTimer;
    bool wakeArmed = false;
    urlStandard standard;

    map<int, pageRecord> pages;
    map<pair<int, int>, edgeRecord> edges;
    unordered_map<string, int> urlToId;
    unordered_map<string, int> hashToId;
    unordered_map<string, int> hostToId;
    map<int, hostRecord> hosts;
    map<string, siteRecord> sites;
    int nextPageId = 0;
    int nextHostId = 0;

    map<int, map<string, deque<int>>> queue;    // depth -> site -> page ids
    string lastSiteServed;
    int globalInFlight = 0;
    int pagesStarted = 0;
    int runNumber = 0;
    ofstream logFile;
    string dataPath;

    void enqueue(int pageId, bool atFront = false);
    void scheduleNext();
    bool canStartRequest(siteRecord& site);
    void startFetch(int pageId);
    void handleRateLimit(siteRecord& site, int retryAfterSeconds);
    bool hasWaitingWork();
    void armWakeTimer();

    void onFetchComplete(int pageId, fetchResult result);
    void recordResultFields(pageRecord& page, fetchResult& result);
    void retryOrFail(int pageId, fetchResult& result);
    int handleRedirectResult(int pageId, fetchResult& result);
    void processHtml(int pageId, fetchResult& result);
    void recordEdges(int fromId, vector<extractedLink>& links, string pageUrl, string baseHref);
    int getOrCreatePage(normalizedUrl n, int depth);
    int getOrCreateHost(string name);
    void updateHostDns(int hostId, string fetchUrl);
    bool resourceLink(string path);

    int applyContentHash(int pageId, string hashableBody);
    string computeSha256(string text);
    void mergePages(int survivorId, int mergedId, string mergeType);

    void saveToJson();
    bool loadFromJson();
    void validateLoaded();
    void logEvent(string type, string message);
    string timeNow();
    pageRecord* findPage(int pageId);
    int resolveMerged(int pageId);
    string statusToText(pageStatus status);
    pageStatus textToStatus(string text);

public:
    crawler(crawlConfig config);
    vector<string> run();

    // for part 2 and part 3
    int lookupId(string keyOrUrl);
    string lookupKey(int pageId);
    const unordered_map<string, int>& getUrlToIdMap();
    const map<int, pageRecord>& getPages();
    const map<pair<int, int>, edgeRecord>& getEdges();
    const map<int, hostRecord>& getHosts();
    const map<string, siteRecord>& getSites();
    bool isOnGraph(int pageId);
};

#endif //MCGUFFIN_CRAWLER_H