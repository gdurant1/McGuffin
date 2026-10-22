#include <algorithm>
#include <climits>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <openssl/evp.h>
#include <boost/json.hpp>
#include "crawler.h"

using namespace std;
namespace json = boost::json;

// members are built in declaration order: io_context first, then what runs on it
crawler::crawler(crawlConfig config)
    : config(config), context(), resolver(context),
      pageFetcher(context, resolver, config.timeoutSeconds, config.maxBodyBytes, config.maxRedirects),
      wakeTimer(context) {}

// load any earlier crawl of the seed, crawl to the page cap, save, return the fetch urls
vector<string> crawler::run() {
    optional<normalizedUrl> seed = this->standard.normalize(this->config.seedUrl);
    if (!seed.has_value()) {
        throw runtime_error("seed url is not a valid http/https url: " + this->config.seedUrl);
    }
    error_code folderError;
    filesystem::create_directories(this->config.outputFolder, folderError);
    if (folderError) {
        throw runtime_error("can't create output folder " + this->config.outputFolder);
    }

    // file name from the seed key: site.com/docs -> site.com_docs
    string baseName;
    for (char c : seed.value().key) {
        if (isalnum((unsigned char)c) || c == '.' || c == '-') {
            baseName += c;
        }
        else {
            baseName += '_';
        }
    }
    while (!baseName.empty() && baseName.back() == '_') {
        baseName.pop_back();
    }
    this->dataPath = this->config.outputFolder + "/" + baseName + ".json";
    string logPath = this->config.outputFolder + "/" + baseName + ".log";
    this->logFile.open(logPath, ios::app);
    if (!this->logFile) {
        throw runtime_error("can't open log file " + logPath);
    }

    bool loaded = this->loadFromJson();
    this->runNumber++;
    this->logFile << "\n===== Run " << this->runNumber << " | seed: " << this->config.seedUrl
                  << " | " << this->timeNow() << " =====" << endl;
    if (loaded) {
        this->logEvent("LOAD", this->dataPath + " (" + to_string(this->pages.size()) + " pages)");
    }

    int seedId = this->getOrCreatePage(seed.value(), 0);
    this->pages[seedId].status = pageStatus::queued;
    this->pages[seedId].depth = 0;
    for (auto& entry : this->pages) {
        pageRecord& page = entry.second;
        if (page.mergedInto == -1 && (page.status == pageStatus::queued || page.status == pageStatus::unfetched)) {
            page.status = pageStatus::queued;
            this->enqueue(page.id);
        }
    }

    this->scheduleNext();
    this->context.run();

    // pages still waiting become unfetched nodes
    vector<string> result;
    for (auto& entry : this->pages) {
        if (entry.second.status == pageStatus::queued) {
            entry.second.status = pageStatus::unfetched;
        }
        if (this->isOnGraph(entry.first)) {
            result.push_back(entry.second.fetchUrl);
        }
    }
    this->saveToJson();
    return result;
}

// add a page to the queue under its depth and site; retries go to the front
void crawler::enqueue(int pageId, bool atFront) {
    pageRecord& page = this->pages[pageId];
    deque<int>& waiting = this->queue[page.depth][this->hosts[page.hostId].site];
    if (atFront) {
        waiting.push_front(pageId);
    }
    else {
        waiting.push_back(pageId);
    }
}

// start as many requests as allowed: shallowest depth first, round-robin across sites
void crawler::scheduleNext() {
    bool started = true;
    while (started && this->globalInFlight < this->config.globalLimit) {
        started = false;
        for (auto depthIt = this->queue.begin(); depthIt != this->queue.end() && !started;) {
            map<string, deque<int>>& bySite = depthIt->second;
            auto siteIt = bySite.upper_bound(this->lastSiteServed);
            int sitesToCheck = (int)bySite.size();
            for (int n = 0; n < sitesToCheck && !started && !bySite.empty(); n++) {
                if (siteIt == bySite.end()) {
                    siteIt = bySite.begin();
                }
                deque<int>& waiting = siteIt->second;
                while (!waiting.empty()) {
                    pageRecord* front = this->findPage(waiting.front());
                    if (front != nullptr && front->mergedInto == -1 && front->status == pageStatus::queued && !front->inProgress) {
                        break;
                    }
                    waiting.pop_front();
                }
                if (!waiting.empty()) {
                    pageRecord& page = this->pages[waiting.front()];
                    bool firstAttempt = page.retryCount == 0 && page.rateLimitCount == 0;
                    if ((!firstAttempt || this->pagesStarted < this->config.pageCap) && this->canStartRequest(this->sites[siteIt->first])) {
                        int pageId = waiting.front();
                        waiting.pop_front();
                        this->lastSiteServed = siteIt->first;
                        this->startFetch(pageId);
                        started = true;
                    }
                }
                if (waiting.empty()) {
                    siteIt = bySite.erase(siteIt);
                }
                else {
                    ++siteIt;
                }
            }
            if (bySite.empty()) {
                depthIt = this->queue.erase(depthIt);
            }
            else {
                ++depthIt;
            }
        }
    }
    this->armWakeTimer();
}

// a site may start a request if it has a free slot, its delay passed and it isn't backing off
bool crawler::canStartRequest(siteRecord& site) {
    auto now = chrono::steady_clock::now();
    return site.inFlight < this->config.siteLimit
        && now >= site.lastRequest + chrono::milliseconds(this->config.siteDelayMs)
        && now >= site.backoffUntil;
}

// start one download; only first attempts count toward the page cap
void crawler::startFetch(int pageId) {
    pageRecord& page = this->pages[pageId];
    siteRecord& site = this->sites[this->hosts[page.hostId].site];
    if (page.retryCount == 0 && page.rateLimitCount == 0) {
        this->pagesStarted++;
    }
    page.inProgress = true;
    site.inFlight++;
    site.lastRequest = chrono::steady_clock::now();
    this->globalInFlight++;
    this->pageFetcher.download(page.fetchUrl, [this, pageId](fetchResult result) {
        boost::asio::post(this->context, [this, pageId, result]() {
            this->onFetchComplete(pageId, result);
        });
    });
}

// 429: wait retry-after seconds, or double the wait (1 s, 2 s, ... 60 s)
void crawler::handleRateLimit(siteRecord& site, int retryAfterSeconds) {
    if (retryAfterSeconds > 0) {
        site.backoffMs = retryAfterSeconds * 1000;
    }
    else {
        site.backoffMs = min(max(site.backoffMs * 2, 1000), 60000);
    }
    site.backoffUntil = chrono::steady_clock::now() + chrono::milliseconds(site.backoffMs);
    this->logEvent("BACKOFF", site.domain + " waiting " + to_string(site.backoffMs) + " ms");
}

// true if any queued page could still start later
bool crawler::hasWaitingWork() {
    for (auto& depthEntry : this->queue) {
        for (auto& siteEntry : depthEntry.second) {
            for (int pageId : siteEntry.second) {
                pageRecord* page = this->findPage(pageId);
                if (page != nullptr && page->status == pageStatus::queued && !page->inProgress && page->mergedInto == -1
                    && (page->retryCount > 0 || page->rateLimitCount > 0 || this->pagesStarted < this->config.pageCap)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// check again in 50 ms when work is waiting on a delay; with no work, context.run() ends
void crawler::armWakeTimer() {
    if (this->wakeArmed || this->globalInFlight >= this->config.globalLimit || !this->hasWaitingWork()) {
        return;
    }
    this->wakeArmed = true;
    this->wakeTimer.expires_after(chrono::milliseconds(50));
    this->wakeTimer.async_wait([this](const boost::system::error_code& error) {
        this->wakeArmed = false;
        if (!error) {
            this->scheduleNext();
        }
    });
}

// record a finished download: retry, back off, follow redirect rules, or read the page
void crawler::onFetchComplete(int pageId, fetchResult result) {
    pageRecord& page = this->pages[pageId];
    siteRecord& site = this->sites[this->hosts[page.hostId].site];
    site.inFlight--;
    this->globalInFlight--;
    page.inProgress = false;
    this->recordResultFields(page, result);
    this->updateHostDns(page.hostId, result.finalUrl.empty() ? page.fetchUrl : result.finalUrl);
    if (!result.certificateOk) {
        this->logEvent("CERT", page.fetchUrl + " " + result.certificateError);
    }

    if (result.statusCode == 429) {
        this->handleRateLimit(site, result.retryAfterSeconds);
        if (page.rateLimitCount < 3) {
            page.rateLimitCount++;
            page.status = pageStatus::queued;
            this->enqueue(pageId, true);
        }
        else {
            page.status = pageStatus::failed;
            this->logEvent("ERROR", page.fetchUrl + " still rate limited after 3 tries");
        }
        this->scheduleNext();
        return;
    }
    if (result.failed || result.statusCode >= 500) {
        this->retryOrFail(pageId, result);
        this->scheduleNext();
        return;
    }
    site.backoffMs = 0;

    int ownerId = this->handleRedirectResult(pageId, result);
    if (ownerId != -1) {
        pageRecord& owner = this->pages[ownerId];
        if (ownerId != pageId) {
            this->recordResultFields(owner, result);
        }
        if (result.statusCode >= 400) {
            owner.status = pageStatus::failed;
            this->logEvent("ERROR", owner.fetchUrl + " HTTP " + to_string(result.statusCode));
        }
        else if (result.tooLarge) {
            owner.status = pageStatus::tooLarge;
            this->logEvent("TOO_LARGE", owner.fetchUrl);
        }
        else if (!result.isHtml) {
            owner.status = pageStatus::nonHtml;
        }
        else {
            this->processHtml(ownerId, result);
        }
    }
    this->scheduleNext();
}

// copy what the download reported onto a page
void crawler::recordResultFields(pageRecord& page, fetchResult& result) {
    page.lastFetched = this->timeNow();
    page.httpStatus = result.statusCode;
    page.contentType = result.contentType;
    page.certificateOk = result.certificateOk;
    page.certificateError = result.certificateError;
    page.error = result.error;
}

// timeouts, connection failures and 5xx get one retry, then the page is failed
void crawler::retryOrFail(int pageId, fetchResult& result) {
    pageRecord& page = this->pages[pageId];
    string reason = result.error.empty() ? "HTTP " + to_string(result.statusCode) : result.error;
    if (page.retryCount < 1) {
        page.retryCount++;
        page.status = pageStatus::queued;
        this->enqueue(pageId, true);
        this->logEvent("RETRY", page.fetchUrl + " " + reason);
    }
    else {
        page.status = pageStatus::failed;
        this->logEvent("ERROR", page.fetchUrl + " " + reason);
    }
}

// same site or homepage redirect: one page (alias); otherwise an edge to the target
// returns the page that owns the content, or -1 if it was already handled
int crawler::handleRedirectResult(int pageId, fetchResult& result) {
    if (result.redirectChain.empty() || result.finalUrl.empty()) {
        return pageId;
    }
    optional<normalizedUrl> target = this->standard.normalize(result.finalUrl);
    pageRecord& page = this->pages[pageId];
    if (!target.has_value() || target.value().key == page.key) {
        return pageId;
    }

    optional<normalizedUrl> source = this->standard.normalize(page.fetchUrl);
    bool sameSite = target.value().site == this->hosts[page.hostId].site;
    bool homepage = source.has_value() && this->standard.isHomepage(source.value());
    auto existing = this->urlToId.find(target.value().key);

    if (sameSite || homepage) {
        if (existing != this->urlToId.end() && this->resolveMerged(existing->second) != pageId) {
            int survivorId = this->resolveMerged(existing->second);
            this->mergePages(survivorId, pageId, "redirect");
            pageRecord& survivor = this->pages[survivorId];
            if (survivor.status != pageStatus::queued || survivor.inProgress) {
                return -1;
            }
            return survivorId;
        }
        string oldKey = page.key;
        page.aliases.push_back({oldKey, "redirect"});
        page.key = target.value().key;
        page.fetchUrl = target.value().fetchUrl;
        page.hostId = this->getOrCreateHost(target.value().host);
        this->urlToId[page.key] = pageId;
        this->logEvent("MERGE", oldKey + " -> " + page.key + " (redirect)");
        return pageId;
    }

    int targetId = this->resolveMerged(this->getOrCreatePage(target.value(), page.depth + 1));
    page.status = pageStatus::fetched;
    page.error = "redirected to " + target.value().fetchUrl;
    this->edges[{pageId, targetId}] = {pageId, targetId, 1, false};
    this->logEvent("REDIRECT", page.fetchUrl + " -> " + target.value().fetchUrl);
    pageRecord& targetPage = this->pages[targetId];
    if (targetPage.status != pageStatus::queued || targetPage.inProgress) {
        return -1;
    }
    return targetId;
}

// hash the page, merge it if it's a duplicate, otherwise record its links
void crawler::processHtml(int pageId, fetchResult& result) {
    pageRecord& page = this->pages[pageId];
    extractionResult extracted = extractLinks(result.body);
    page.contentHash = this->computeSha256(extracted.hashableBody);
    page.status = pageStatus::fetched;
    if (this->applyContentHash(pageId, extracted.hashableBody) != pageId) {
        return;
    }
    if (!extracted.metaRefreshUrl.empty()) {
        extracted.links.push_back({extracted.metaRefreshUrl, false});
    }
    string pageUrl = result.finalUrl.empty() ? page.fetchUrl : result.finalUrl;
    this->recordEdges(pageId, extracted.links, pageUrl, extracted.baseHref);
}

// count links per target (weight), replace the page's old edges, queue new pages
void crawler::recordEdges(int fromId, vector<extractedLink>& links, string pageUrl, string baseHref) {
    map<int, int> counts;
    map<int, bool> allNavigation;
    int fromDepth = this->pages[fromId].depth;
    for (int i = 0; i < (int)links.size(); i++) {
        optional<normalizedUrl> n = this->standard.normalize(pageUrl, baseHref, links[i].url);
        if (!n.has_value() || this->isResourceLink(n.value().path)) {
            continue;
        }
        int pagesBefore = this->nextPageId;
        int targetId = this->resolveMerged(this->getOrCreatePage(n.value(), fromDepth + 1));
        if (this->nextPageId > pagesBefore) {
            this->enqueue(targetId);
        }
        if (targetId == fromId) {
            continue;
        }
        if (counts.count(targetId) == 0) {
            allNavigation[targetId] = links[i].inNavigation;
        }
        allNavigation[targetId] = allNavigation[targetId] && links[i].inNavigation;
        counts[targetId]++;
    }

    this->edges.erase(this->edges.lower_bound({fromId, INT_MIN}), this->edges.lower_bound({fromId + 1, INT_MIN}));
    for (auto& entry : counts) {
        this->edges[{fromId, entry.first}] = {fromId, entry.first, entry.second, allNavigation[entry.first]};
    }
}

// page id for a normalized url; creates the page if it's new
int crawler::getOrCreatePage(normalizedUrl n, int depth) {
    auto found = this->urlToId.find(n.key);
    if (found != this->urlToId.end()) {
        pageRecord& page = this->pages[this->resolveMerged(found->second)];
        if (page.originalUrls.size() < 10 && find(page.originalUrls.begin(), page.originalUrls.end(), n.original) == page.originalUrls.end()) {
            page.originalUrls.push_back(n.original);
        }
        page.depth = min(page.depth, depth);
        return page.id;
    }
    pageRecord page;
    page.id = this->nextPageId++;
    page.key = n.key;
    page.fetchUrl = n.fetchUrl;
    page.originalUrls.push_back(n.original);
    page.hostId = this->getOrCreateHost(n.host);
    page.depth = depth;
    this->pages[page.id] = page;
    this->urlToId[page.key] = page.id;
    return page.id;
}

// host id for a hostname; creates the host and its site if new
int crawler::getOrCreateHost(string name) {
    auto found = this->hostToId.find(name);
    if (found != this->hostToId.end()) {
        return found->second;
    }
    hostRecord host;
    host.id = this->nextHostId++;
    host.name = name;
    host.site = this->standard.registrableDomain(name);
    this->hosts[host.id] = host;
    this->hostToId[name] = host.id;
    this->sites[host.site].domain = host.site;
    this->sites[host.site].hostIds.push_back(host.id);
    return host.id;
}

// copy cached dns results onto the host the first time
void crawler::updateHostDns(int hostId, string fetchUrl) {
    hostRecord& host = this->hosts[hostId];
    optional<url> parsed = parseUrl(fetchUrl);
    if (!host.ipv4.empty() || !host.ipv6.empty() || !parsed.has_value()) {
        return;
    }
    optional<dnsInfo> dns = this->resolver.getCached(toLowerText(parsed.value().host));
    if (dns.has_value() && !dns.value().failed) {
        host.ipv4 = dns.value().ipv4;
        host.ipv6 = dns.value().ipv6;
        host.canonicalName = dns.value().canonicalName;
        host.noIpv6 = dns.value().noIpv6;
    }
}

// images, scripts, styles, documents and media are skipped before fetching
bool crawler::isResourceLink(string path) {
    static const vector<string> extensions = {
        ".jpg", ".jpeg", ".png", ".gif", ".svg", ".webp", ".ico", ".bmp", ".tif", ".tiff",
        ".css", ".js", ".mjs", ".json", ".xml", ".rss", ".atom",
        ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".csv", ".txt",
        ".zip", ".gz", ".tar", ".rar", ".7z", ".exe", ".dmg", ".iso", ".apk",
        ".mp3", ".wav", ".ogg", ".mp4", ".webm", ".avi", ".mov", ".mkv",
        ".woff", ".woff2", ".ttf", ".otf", ".eot"};
    string lastPart = toLowerText(path.substr(path.rfind('/') + 1));
    int dot = (int)lastPart.rfind('.');
    return dot != (int)string::npos && find(extensions.begin(), extensions.end(), lastPart.substr(dot)) != extensions.end();
}

// merge into an earlier identical page (status 200, body at least 5 kb); returns the owner id
int crawler::applyContentHash(int pageId, string hashableBody) {
    pageRecord& page = this->pages[pageId];
    if (page.httpStatus != 200 || (int)hashableBody.size() < this->config.minHashBytes) {
        return pageId;
    }
    auto found = this->hashToId.find(page.contentHash);
    if (found != this->hashToId.end() && this->resolveMerged(found->second) != pageId) {
        int survivorId = this->resolveMerged(found->second);
        this->mergePages(survivorId, pageId, "contentHash");
        return survivorId;
    }
    this->hashToId[page.contentHash] = pageId;
    return pageId;
}

// sha-256 as a hex string
string crawler::computeSha256(string text) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_Digest(text.data(), text.size(), digest, &length, EVP_sha256(), nullptr);
    ostringstream out;
    for (unsigned int i = 0; i < length; i++) {
        out << std::hex << setw(2) << setfill('0') << (int)digest[i];
    }
    return out.str();
}

// survivor takes the merged page's names and incoming links (weights add); its outgoing links are dropped
void crawler::mergePages(int survivorId, int mergedId, string mergeType) {
    pageRecord& survivor = this->pages[survivorId];
    pageRecord& merged = this->pages[mergedId];
    survivor.aliases.push_back({merged.key, mergeType});
    survivor.aliases.insert(survivor.aliases.end(), merged.aliases.begin(), merged.aliases.end());
    for (string& original : merged.originalUrls) {
        if (find(survivor.originalUrls.begin(), survivor.originalUrls.end(), original) == survivor.originalUrls.end()) {
            survivor.originalUrls.push_back(original);
        }
    }
    survivor.depth = min(survivor.depth, merged.depth);
    this->urlToId[merged.key] = survivorId;
    for (aliasRecord& alias : merged.aliases) {
        this->urlToId[alias.key] = survivorId;
    }
    merged.mergedInto = survivorId;

    vector<edgeRecord> movedIn;
    for (auto it = this->edges.begin(); it != this->edges.end();) {
        if (it->second.to == mergedId && it->second.from != survivorId) {
            movedIn.push_back(it->second);
        }
        if (it->second.from == mergedId || it->second.to == mergedId) {
            it = this->edges.erase(it);
        }
        else {
            ++it;
        }
    }
    for (edgeRecord& edge : movedIn) {
        edgeRecord& target = this->edges[{edge.from, survivorId}];
        if (target.weight == 0) {
            target = {edge.from, survivorId, 0, true};
        }
        target.weight += edge.weight;
        target.navigationByLocation = target.navigationByLocation && edge.navigationByLocation;
    }
    this->logEvent("MERGE", merged.key + " -> " + survivor.key + " (" + mergeType + ")");
}

// write the whole crawl to one json file (merged pages are saved as aliases)
void crawler::saveToJson() {
    json::object root;
    root["crawl"] = {{"seed", this->config.seedUrl}, {"runs", this->runNumber},
                     {"normalizationVersion", this->standard.normalizationVersion()},
                     {"nextPageId", this->nextPageId}, {"nextHostId", this->nextHostId},
                     {"pageCap", this->config.pageCap}, {"savedAt", this->timeNow()}};

    json::array siteList;
    for (auto& entry : this->sites) {
        siteList.push_back({{"domain", entry.second.domain},
                            {"hostIds", json::value_from(entry.second.hostIds)}});
    }
    root["sites"] = siteList;

    json::array hostList;
    for (auto& entry : this->hosts) {
        hostRecord& host = entry.second;
        hostList.push_back({{"id", host.id}, {"name", host.name}, {"site", host.site},
                            {"ipv4", json::value_from(host.ipv4)}, {"ipv6", json::value_from(host.ipv6)},
                            {"canonicalName", host.canonicalName}, {"noIpv6", host.noIpv6}});
    }
    root["hosts"] = hostList;

    json::array pageList;
    for (auto& entry : this->pages) {
        pageRecord& page = entry.second;
        if (page.mergedInto != -1) {
            continue;
        }
        json::array aliases;
        for (aliasRecord& alias : page.aliases) {
            aliases.push_back({{"key", alias.key}, {"mergeType", alias.mergeType}});
        }
        pageList.push_back({{"id", page.id}, {"key", page.key}, {"fetchUrl", page.fetchUrl},
                            {"originalUrls", json::value_from(page.originalUrls)}, {"aliases", aliases},
                            {"hostId", page.hostId}, {"depth", page.depth}, {"status", this->statusToText(page.status)},
                            {"httpStatus", page.httpStatus}, {"contentType", page.contentType},
                            {"contentHash", page.contentHash}, {"certificateOk", page.certificateOk},
                            {"certificateError", page.certificateError}, {"error", page.error},
                            {"lastFetched", page.lastFetched}});
    }
    root["pages"] = pageList;

    json::array edgeList;
    for (auto& entry : this->edges) {
        edgeRecord& edge = entry.second;
        if (this->isOnGraph(edge.from) && this->isOnGraph(edge.to)) {
            edgeList.push_back({{"from", edge.from}, {"to", edge.to}, {"weight", edge.weight},
                                {"navigationByLocation", edge.navigationByLocation}});
        }
    }
    root["edges"] = edgeList;

    ofstream out(this->dataPath);
    if (!out) {
        this->logEvent("ERROR", "can't write " + this->dataPath);
        return;
    }
    out << json::serialize(root);
    this->logEvent("SAVE", this->dataPath);
}

// read a json field, or a default when it's missing or the wrong type
static string readText(const json::object& item, string name) {
    const json::value* value = item.if_contains(name);
    return value != nullptr && value->is_string() ? string(value->as_string()) : "";
}

static int readInt(const json::object& item, string name, int fallback) {
    const json::value* value = item.if_contains(name);
    return value != nullptr && value->is_int64() ? (int)value->as_int64() : fallback;
}

static bool readBool(const json::object& item, string name, bool fallback) {
    const json::value* value = item.if_contains(name);
    return value != nullptr && value->is_bool() ? value->as_bool() : fallback;
}

static vector<string> readTextList(const json::object& item, string name) {
    vector<string> result;
    const json::value* value = item.if_contains(name);
    if (value != nullptr && value->is_array()) {
        for (const json::value& entry : value->as_array()) {
            if (entry.is_string()) {
                result.push_back(string(entry.as_string()));
            }
        }
    }
    return result;
}

static const json::array& readArray(const json::object& data, const char* name) {
    static const json::array empty;
    const json::value* value = data.if_contains(name);
    return value != nullptr && value->is_array() ? value->as_array() : empty;
}

// load an earlier crawl of this seed; a file that can't be parsed stops the program
bool crawler::loadFromJson() {
    if (!filesystem::exists(this->dataPath)) {
        return false;
    }
    ifstream in(this->dataPath);
    ostringstream text;
    text << in.rdbuf();
    json::value root;
    try {
        root = json::parse(text.str());
    }
    catch (const exception& error) {
        throw runtime_error("saved crawl " + this->dataPath + " is corrupted: " + error.what());
    }
    if (!root.is_object()) {
        throw runtime_error("saved crawl " + this->dataPath + " is not a json object");
    }
    const json::object& data = root.as_object();

    if (data.contains("crawl") && data.at("crawl").is_object()) {
        const json::object& crawlInfo = data.at("crawl").as_object();
        this->runNumber = readInt(crawlInfo, "runs", 0);
        this->nextPageId = readInt(crawlInfo, "nextPageId", 0);
        this->nextHostId = readInt(crawlInfo, "nextHostId", 0);
    }

    for (const json::value& value : readArray(data, "hosts")) {
        if (!value.is_object() || readInt(value.as_object(), "id", -1) < 0 || readText(value.as_object(), "name").empty()) {
            continue;
        }
        const json::object& item = value.as_object();
        hostRecord host;
        host.id = readInt(item, "id", -1);
        host.name = readText(item, "name");
        host.site = this->standard.registrableDomain(host.name);
        host.ipv4 = readTextList(item, "ipv4");
        host.ipv6 = readTextList(item, "ipv6");
        host.canonicalName = readText(item, "canonicalName");
        host.noIpv6 = readBool(item, "noIpv6", false);
        this->hosts[host.id] = host;
    }

    for (const json::value& value : readArray(data, "pages")) {
        if (!value.is_object()) {
            continue;
        }
        const json::object& item = value.as_object();
        pageRecord page;
        page.id = readInt(item, "id", -1);
        page.key = readText(item, "key");
        page.hostId = readInt(item, "hostId", -1);
        if (page.id < 0 || page.key.empty() || this->hosts.count(page.hostId) == 0) {
            this->logEvent("LOAD", "skipped a page with a missing id, key or host");
            continue;
        }
        page.fetchUrl = readText(item, "fetchUrl");
        page.originalUrls = readTextList(item, "originalUrls");
        for (const json::value& alias : readArray(item, "aliases")) {
            if (alias.is_object() && !readText(alias.as_object(), "key").empty()) {
                page.aliases.push_back({readText(alias.as_object(), "key"), readText(alias.as_object(), "mergeType")});
            }
        }
        page.depth = readInt(item, "depth", 0);
        page.status = this->textToStatus(readText(item, "status"));
        page.httpStatus = readInt(item, "httpStatus", 0);
        page.contentType = readText(item, "contentType");
        page.contentHash = readText(item, "contentHash");
        page.certificateOk = readBool(item, "certificateOk", true);
        page.certificateError = readText(item, "certificateError");
        page.error = readText(item, "error");
        page.lastFetched = readText(item, "lastFetched");
        this->pages[page.id] = page;
    }

    for (const json::value& value : readArray(data, "edges")) {
        if (!value.is_object()) {
            continue;
        }
        const json::object& item = value.as_object();
        edgeRecord edge = {readInt(item, "from", -1), readInt(item, "to", -1), readInt(item, "weight", 1),
                           readBool(item, "navigationByLocation", false)};
        if (this->findPage(edge.from) != nullptr && this->findPage(edge.to) != nullptr && edge.weight >= 1) {
            this->edges[{edge.from, edge.to}] = edge;
        }
    }
    this->validateLoaded();
    return true;
}

// rebuild lookup maps after loading; a key saved twice is merged
void crawler::validateLoaded() {
    for (auto& entry : this->hosts) {
        hostRecord& host = entry.second;
        this->hostToId[host.name] = host.id;
        this->sites[host.site].domain = host.site;
        this->sites[host.site].hostIds.push_back(host.id);
        this->nextHostId = max(this->nextHostId, host.id + 1);
    }
    vector<int> pageIds;
    for (auto& entry : this->pages) {
        pageIds.push_back(entry.first);
    }
    for (int pageId : pageIds) {
        pageRecord& page = this->pages[pageId];
        this->nextPageId = max(this->nextPageId, page.id + 1);
        auto found = this->urlToId.find(page.key);
        if (found != this->urlToId.end() && this->resolveMerged(found->second) != page.id) {
            this->logEvent("DUPLICATE", "key " + page.key + " was saved twice");
            this->mergePages(this->resolveMerged(found->second), page.id, "load");
            continue;
        }
        this->urlToId[page.key] = page.id;
        for (aliasRecord& alias : page.aliases) {
            this->urlToId[alias.key] = page.id;
        }
        if (!page.contentHash.empty() && page.status == pageStatus::fetched) {
            this->hashToId[page.contentHash] = page.id;
        }
    }
}

// one log line: [time] type message
void crawler::logEvent(string type, string message) {
    if (this->logFile.is_open()) {
        this->logFile << "[" << this->timeNow() << "] " << type << " " << message << endl;
    }
}

// local date and time: 2026-10-20 23:10:00
string crawler::timeNow() {
    time_t now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    ostringstream out;
    out << put_time(localtime(&now), "%Y-%m-%d %H:%M:%S");
    return out.str();
}

// pointer to a page, or nullptr
pageRecord* crawler::findPage(int pageId) {
    auto found = this->pages.find(pageId);
    return found == this->pages.end() ? nullptr : &found->second;
}

// follow merges to the surviving page
int crawler::resolveMerged(int pageId) {
    pageRecord* page = this->findPage(pageId);
    while (page != nullptr && page->mergedInto != -1) {
        pageId = page->mergedInto;
        page = this->findPage(pageId);
    }
    return pageId;
}

// status names used in the json file
string crawler::statusToText(pageStatus status) {
    const string names[] = {"queued", "fetched", "failed", "unfetched", "tooLarge", "nonHtml"};
    return names[(int)status];
}

// unknown or "queued" becomes unfetched so it's fetched next run
pageStatus crawler::textToStatus(string text) {
    const string names[] = {"queued", "fetched", "failed", "unfetched", "tooLarge", "nonHtml"};
    for (int i = 1; i < 6; i++) {
        if (text == names[i]) {
            return (pageStatus)i;
        }
    }
    return pageStatus::unfetched;
}

// page id for a key, alias or url; -1 if unknown
int crawler::lookupId(string keyOrUrl) {
    auto found = this->urlToId.find(keyOrUrl);
    if (found == this->urlToId.end()) {
        optional<normalizedUrl> n = this->standard.normalize(keyOrUrl);
        if (!n.has_value()) {
            return -1;
        }
        found = this->urlToId.find(n.value().key);
    }
    return found == this->urlToId.end() ? -1 : this->resolveMerged(found->second);
}

// key for a page id; "" if unknown
string crawler::lookupKey(int pageId) {
    pageRecord* page = this->findPage(this->resolveMerged(pageId));
    return page == nullptr ? "" : page->key;
}

const unordered_map<string, int>& crawler::getUrlToIdMap() {
    return this->urlToId;
}

const map<int, pageRecord>& crawler::getPages() {
    return this->pages;
}

const map<pair<int, int>, edgeRecord>& crawler::getEdges() {
    return this->edges;
}

const map<int, hostRecord>& crawler::getHosts() {
    return this->hosts;
}

const map<string, siteRecord>& crawler::getSites() {
    return this->sites;
}

// on the graph = not merged and not a non-html file
bool crawler::isOnGraph(int pageId) {
    pageRecord* page = this->findPage(pageId);
    return page != nullptr && page->mergedInto == -1 && page->status != pageStatus::nonHtml;
}