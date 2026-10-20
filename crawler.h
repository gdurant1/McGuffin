#ifndef MCGUFFIN_CRAWLER_H
#define MCGUFFIN_CRAWLER_H

#include <chrono>           // steady_clock - delays and back-off
#include <deque>            // deque - pages waiting per site
#include <fstream>          // ofstream - log file
#include <map>              // map - pages, edges, queue
#include <string>           // string
#include <unordered_map>    // unordered_map - URL-to-ID map, hosts, sites
#include <utility>          // pair - edge keys
#include <vector>           // vector
#include <boost/asio.hpp>   // io_context, steady_timer
#include "dnsResolver.h"
#include "fetcher.h"
#include "linkExtractor.h"
#include "urlStandard.h"

using namespace std;

// limits for crawl
struct crawlConfig {
    string seedUrl;                         // where the crawl starts
    int pageCap = 1000;                     // most pages fetched per run
    int globalLimit = 8;                    // most requests in flight at once
    int siteStartLimit = 1;                 // requests in flight per site at the start
    int siteMaxLimit = 2;                   // most requests in flight per site
    int siteDelayMs = 250;                  // wait between requests to the same site
    int timeoutSeconds = 10;                // per request
    int maxRedirects = 5;                   // redirect hops followed
    int maxBodyBytes = 5 * 1024 * 1024;     // 5 MB, larger pages marked "too large"
    int minHashBytes = 5 * 1024;            // 5 KB, smaller bodies are never merged by content
    double navigationThreshold = 0.5;       // link on more than 50% of a site's pages = navigation
    int minPagesForNavigation = 5;          // site needs this many fetched pages before frequency counts
    string outputFolder = "data";           // where the JSON and log go
};

// DATA

// What happened to a page
enum class pageStatus {
    queued,     // waiting to be fetched
    fetched,    // fetched successfully
    failed,     // 4xx, 5xx, timeout or connection failure
    unfetched,  // found, but the page cap was reached first
    tooLarge,   // body over 5 MB
    nonHtml,    // not an HTML page - kept off the graph
    blocked     // challenge / verification page detected
};

// Another url that leads to the same page
struct aliasRecord {
    string key;         // the other page key
    string mergeType;   // "redirect" or "contentHash"
};

// One node of the graph
struct pageRecord {
    int id = -1;
    string key;                     // normalized identity
    string fetchUrl;                // address used to fetch
    vector<string> originalUrls;    // urls as found, before normalization
    vector<aliasRecord> aliases;    // other keys merged into this page
    int hostId = -1;
    int depth = 0;                  // link distance from the seed
    pageStatus status = pageStatus::queued;
    int httpStatus = 0;
    string contentType;
    string contentHash;             // SHA-256 of the body without scripts and styles
    bool certificateOk = true;
    string certificateError;
    string error;
    string blockedReason;
    int retryCount = 0;             // 5xx / timeout retries
    int rateLimitCount = 0;         // 429 retries
    bool inProgress = false;        // a request for this page is running
    string lastFetched;             // date&time of the last fetch
    int mergedInto = -1;            // set when this page was merged into another one
};

// One edge of the graph
struct edgeRecord {
    int from = -1;
    int to = -1;
    int weight = 0;                 // number of links from "from" to "to"
    bool navigationByLocation = false;  // every link was inside nav/header/footer/aside
    bool navigationByFrequency = false; // target is linked from most pages of the site
};

// One hostname
struct hostRecord {
    int id = -1;
    string name;                    // hostname after the www merge
    string site;                    // registrable domain
    vector<string> ipv4;
    vector<string> ipv6;
    string canonicalName;
    bool noIpv6 = false;
    caseRule pathCase = caseRule::unknown;
    bool caseProbeStarted = false;
};

// One site (registrable domain) and its request scheduling state
struct siteRecord {
    string domain;
    vector<int> hostIds;
    int inFlight = 0;               // requests running right now
    int limit = 1;                  // adaptive limit, between start and max
    int cleanStreak = 0;            // clean responses in a row (raises the limit)
    chrono::steady_clock::time_point lastRequest;   // for the 250 ms delay
    chrono::steady_clock::time_point backoffUntil;  // after a 429
    int backoffMs = 0;              // current back-off length (doubles)
};

// A fetch that checks whether a host's paths are case-sensitive
struct caseProbe {
    int hostId = -1;
    int pageId = -1;                // the page whose content is compared
    string probeUrl;                // same page with the path's letter case flipped
};

class crawler {
private:
    // settings
    crawlConfig config;

    // async machinery (single thread: everything runs inside context.run())
    boost::asio::io_context context;
    dnsResolver resolver;
    fetcher pageFetcher;
    boost::asio::steady_timer wakeTimer;
    bool wakeArmed = false;

    // normalization rules
    urlStandard standard;

    // the graph
    map<int, pageRecord> pages;                 // page ID -> page
    map<pair<int, int>, edgeRecord> edges;      // (from, to) -> edge
    unordered_map<string, int> urlToId;         // key or alias -> page ID
    unordered_map<string, int> hashToId;        // content hash -> page ID
    unordered_map<string, int> hostToId;        // hostname -> host ID
    map<int, hostRecord> hosts;                 // host ID -> host
    map<string, siteRecord> sites;              // domain -> site (map: references stay valid when sites are added)
    int nextPageId = 0;
    int nextHostId = 0;

    // scheduling
    map<int, map<string, deque<int>>> queue;    // depth -> site -> waiting page IDs
    string lastSiteServed;                      // for round-robin across sites
    vector<caseProbe> pendingProbes;            // case tests waiting to run
    int globalInFlight = 0;
    int pagesStarted = 0;                       // counts toward the page cap

    // run info
    int runNumber = 0;
    int seedId = -1;
    ofstream logFile;
    string dataPath;
    string logPath;

    // SCHEDULING
    void enqueue(int pageId, bool atFront = false);
    bool hasWaitingWork();
    void scheduleNext();
    bool canStartRequest(siteRecord& site);
    void startFetch(int pageId);
    void startProbe(caseProbe probe);
    void adjustConcurrency(siteRecord& site, bool clean);
    void handleRateLimit(siteRecord& site, int retryAfterSeconds);
    bool shouldRetry(pageRecord& page, fetchResult& result);
    void armWakeTimer();

    // HANDLING A FETCHED PAGE
    void onFetchComplete(int pageId, fetchResult result);
    void onProbeComplete(caseProbe probe, fetchResult result);
    int handleRedirectResult(int pageId, fetchResult& result);
    void recordResultFields(pageRecord& page, fetchResult& result);
    void retryOrFail(int pageId, fetchResult& result);
    void processHtml(int pageId, fetchResult& result);
    void recordEdges(int fromId, vector<extractedLink>& links, string pageUrl, string baseHref);
    int getOrCreatePage(urlStandard n, int depth);
    int getOrCreateHost(string name);
    siteRecord& getOrCreateSite(string domain);
    void updateHostDns(int hostId, string fetchUrl);
    void queueCaseProbe(int pageId);
    bool isResourceLink(string path);

    // MERGING
    int applyContentHash(int pageId, string hashableBody);
    string computeSha256(string text);
    void mergePages(int survivorId, int mergedId, string mergeType);

    // FINISHING THE GRAPH
    void markUnfetched();
    void markNavigationByFrequency();

    // STORAGE
    string makeFileName(string seedKey);
    void ensureOutputFolder();
    void saveToJson();
    bool loadFromJson();
    void validateLoaded();
    void renormalizeIfOlder(int savedVersion);

    // LOGGING
    void logRunHeader();
    void logEvent(string type, string message);
    string timeNow();

    // HELPERS
    pageRecord* findPage(int pageId);
    int resolveMerged(int pageId);
    string statusToText(pageStatus status);
    pageStatus textToStatus(string text);

public:
    // Constructor: sets up the resolver, fetcher and timer (throws if SSL can't be set up)
    crawler(crawlConfig config);

    // Run the crawl and return the fetch urls of every page on the graph
    vector<string> run();

    // INTERFACE FOR PART 2 AND PART 3
    // Page ID for a key, alias or url; -1 if unknown
    int lookupId(string keyOrUrl);
    // Key for a page ID; "" if unknown
    string lookupKey(int pageId);
    // Read-only access to the graph data
    const unordered_map<string, int>& getUrlToIdMap();
    const map<int, pageRecord>& getPages();
    const map<pair<int, int>, edgeRecord>& getEdges();
    const map<int, hostRecord>& getHosts();
    const map<string, siteRecord>& getSites();
    // True if a page belongs on the graph (not merged, not non-HTML)
    bool isOnGraph(int pageId);
};

#endif //MCGUFFIN_CRAWLER_H