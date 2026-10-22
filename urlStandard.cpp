#include <algorithm>
#include <libpsl.h>
#include "urlStandard.h"

using namespace std;

// index files and tracking parameters to remove
urlStandard::urlStandard() {
    this->indexFiles = {"index.html", "index.htm", "index.php", "default.aspx", "default.htm"};
    this->trackingParameters = {"utm_*", "fbclid", "gclid"};
    this->version = 1;
}

// run every step in order and build the key and fetch url
optional<normalizedUrl> urlStandard::normalize(string pageUrl, string baseHref, string link) {
    if (isSkippableLink(link) || isFragmentOnly(link)) {
        return nullopt;
    }
    optional<url> resolved = this->resolveRelative(pageUrl, baseHref, link);
    if (!resolved.has_value() || !isSupportedScheme(resolved.value().scheme)) {
        return nullopt;
    }

    url u = resolved.value();
    u = this->lowercaseSchemeAndHost(u);
    u = this->dropDefaultPort(u);
    u = this->stripFragment(u);
    u = this->uppercasePercentEncoding(u);
    u = this->normalizeQuery(u);
    u = this->removeIndexFile(u);
    u = this->normalizeTrailingSlash(u);
    url fetchable = u;
    u = this->mergeWww(u);

    normalizedUrl result;
    result.key = this->makeKey(u);
    result.fetchUrl = fetchable.toString();
    result.host = u.host;
    result.site = this->registrableDomain(u.host);
    result.path = u.path;
    result.original = this->stripFragment(resolved.value()).toString();
    return result;
}

// normalize a url that is already absolute
optional<normalizedUrl> urlStandard::normalize(string absoluteUrl) {
    return this->normalize("", "", absoluteUrl);
}

// make a link absolute using the page url, or <base href> when present
optional<url> urlStandard::resolveRelative(string pageUrl, string baseHref, string link) {
    optional<url> base = parseUrl(pageUrl);
    if (!trimText(baseHref).empty()) {
        optional<url> baseFromHref = this->resolveAgainst(base, baseHref);
        if (baseFromHref.has_value()) {
            base = baseFromHref;
        }
    }
    return this->resolveAgainst(base, link);
}

// resolve one link against a base: absolute, //host, ?query, /path or relative path
optional<url> urlStandard::resolveAgainst(optional<url> base, string link) {
    link = trimText(link);
    optional<url> parsed = parseUrl(link);
    if (parsed.has_value()) {
        url result = parsed.value();
        if (result.scheme.empty()) {
            if (!base.has_value()) {
                return nullopt;
            }
            result.scheme = base.value().scheme;
        }
        result.path = this->removeDotSegments(result.path);
        return result;
    }
    if (!base.has_value()) {
        return nullopt;
    }

    string pathPart = link;
    string queryPart;
    bool hasQuery = false;
    url result = base.value();
    result.fragment = "";
    int hashSign = (int)pathPart.find('#');
    if (hashSign != (int)string::npos) {
        result.fragment = pathPart.substr(hashSign + 1);
        pathPart = pathPart.substr(0, hashSign);
    }
    int questionMark = (int)pathPart.find('?');
    if (questionMark != (int)string::npos) {
        queryPart = pathPart.substr(questionMark + 1);
        pathPart = pathPart.substr(0, questionMark);
        hasQuery = true;
    }

    if (pathPart.empty()) {
        if (hasQuery) {
            result.query = queryPart;
        }
    }
    else if (pathPart[0] == '/') {
        result.path = pathPart;
        result.query = queryPart;
    }
    else {
        result.path = result.path.substr(0, result.path.rfind('/') + 1) + pathPart;
        result.query = queryPart;
    }
    result.path = this->removeDotSegments(result.path);
    return result;
}

// remove "." and "..": /a/b/../c/./d -> /a/c/d
string urlStandard::removeDotSegments(string path) {
    vector<string> segments;
    bool endsAsFolder = false;
    int start = 1;
    while (start <= (int)path.size()) {
        int slash = (int)path.find('/', start);
        if (slash == (int)string::npos) {
            slash = (int)path.size();
        }
        string segment = path.substr(start, slash - start);
        bool isLast = slash >= (int)path.size();
        if (segment == ".") {
            endsAsFolder = isLast;
        }
        else if (segment == "..") {
            if (!segments.empty()) {
                segments.pop_back();
            }
            endsAsFolder = isLast;
        }
        else {
            segments.push_back(segment);
            endsAsFolder = false;
        }
        start = slash + 1;
    }

    string result;
    for (int i = 0; i < (int)segments.size(); i++) {
        result += "/" + segments[i];
    }
    if (result.empty()) {
        return "/";
    }
    if (endsAsFolder) {
        result += "/";
    }
    return result;
}

// lowercase scheme and host; path case is kept
url urlStandard::lowercaseSchemeAndHost(url u) {
    u.scheme = toLowerText(u.scheme);
    u.host = toLowerText(u.host);
    return u;
}

// drop 80 on http and 443 on https
url urlStandard::dropDefaultPort(url u) {
    if (u.port == defaultPort(u.scheme)) {
        u.port = 0;
    }
    return u;
}

// remove the #fragment
url urlStandard::stripFragment(url u) {
    u.fragment = "";
    return u;
}

// uppercase the two hex digits after each % in path and query
url urlStandard::uppercasePercentEncoding(url u) {
    string* parts[2] = {&u.path, &u.query};
    for (int p = 0; p < 2; p++) {
        string& text = *parts[p];
        for (int i = 0; i + 2 < (int)text.size(); i++) {
            if (text[i] == '%' && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2])) {
                text[i + 1] = (char)toupper((unsigned char)text[i + 1]);
                text[i + 2] = (char)toupper((unsigned char)text[i + 2]);
            }
        }
    }
    return u;
}

// remove tracking parameters, then sort: ?utm_source=x&b=2&a=1 -> ?a=1&b=2
url urlStandard::normalizeQuery(url u) {
    vector<string> kept;
    int start = 0;
    while (start <= (int)u.query.size()) {
        int amp = (int)u.query.find('&', start);
        if (amp == (int)string::npos) {
            amp = (int)u.query.size();
        }
        string param = u.query.substr(start, amp - start);
        if (!param.empty() && !this->isTrackingParameter(param.substr(0, param.find('=')))) {
            kept.push_back(param);
        }
        start = amp + 1;
    }
    sort(kept.begin(), kept.end());

    u.query = "";
    for (int i = 0; i < (int)kept.size(); i++) {
        if (i > 0) {
            u.query += "&";
        }
        u.query += kept[i];
    }
    return u;
}

// true for names on the tracking list; "utm_*" matches any utm_ name
bool urlStandard::isTrackingParameter(string name) {
    name = toLowerText(name);
    for (int i = 0; i < (int)this->trackingParameters.size(); i++) {
        string tracked = this->trackingParameters[i];
        if (tracked.back() == '*') {
            if (startsWithText(name, tracked.substr(0, tracked.size() - 1))) {
                return true;
            }
        }
        else if (name == tracked) {
            return true;
        }
    }
    return false;
}

// /docs/index.html -> /docs/
url urlStandard::removeIndexFile(url u) {
    int lastSlash = (int)u.path.rfind('/');
    string lastPart = toLowerText(u.path.substr(lastSlash + 1));
    for (int i = 0; i < (int)this->indexFiles.size(); i++) {
        if (lastPart == this->indexFiles[i]) {
            u.path = u.path.substr(0, lastSlash + 1);
            break;
        }
    }
    return u;
}

// /docs/ -> /docs; the root stays "/"
url urlStandard::normalizeTrailingSlash(url u) {
    while (u.path.size() > 1 && u.path.back() == '/') {
        u.path.pop_back();
    }
    if (u.path.empty()) {
        u.path = "/";
    }
    return u;
}

// www.site.com -> site.com
url urlStandard::mergeWww(url u) {
    u.host = this->stripWww(u.host);
    return u;
}

// remove a leading "www." unless nothing would be left
string urlStandard::stripWww(string host) {
    if (startsWithText(host, "www.") && host.find('.', 4) != string::npos) {
        return host.substr(4);
    }
    return host;
}

// key = host, non-default port, path, query (no scheme)
string urlStandard::makeKey(url u) {
    string key = u.host;
    if (u.port != 0) {
        key += ":" + to_string(u.port);
    }
    key += u.path;
    if (!u.query.empty()) {
        key += "?" + u.query;
    }
    return key;
}

// blog.site.com -> site.com; the host itself when libpsl can't decide
string urlStandard::registrableDomain(string host) {
    host = this->stripWww(toLowerText(host));
    const char* domain = psl_registrable_domain(psl_builtin(), host.c_str());
    if (domain == nullptr) {
        return host;
    }
    return string(domain);
}

// homepage = root path (index files are already removed)
bool urlStandard::isHomepage(normalizedUrl n) {
    return n.path == "/";
}

// rules version saved with each crawl
int urlStandard::normalizationVersion() {
    return this->version;
}