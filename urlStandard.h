#ifndef MCGUFFIN_URLSTANDARD_H
#define MCGUFFIN_URLSTANDARD_H

#include <optional>
#include <string>
#include <vector>
#include "url.h"

using namespace std;

struct normalizedUrl {
    string key;         // identity, no scheme: site.com/docs
    string fetchUrl;    // address to fetch, keeps scheme and www
    string host;        // host after the www merge
    string site;        // registrable domain
    string path;
    string original;    // absolute url before normalizing
};

class urlStandard {
private:
    vector<string> indexFiles;
    vector<string> trackingParameters;
    int version;

    optional<url> resolveAgainst(optional<url> base, string link);
    string removeDotSegments(string path);
    bool isTrackingParameter(string name);
    string stripWww(string host);

public:
    urlStandard();

    // normalize a link found on a page; nullopt if it isn't a usable page link
    optional<normalizedUrl> normalize(string pageUrl, string baseHref, string link);
    optional<normalizedUrl> normalize(string absoluteUrl);

    // steps, in the order normalize runs them
    optional<url> resolveRelative(string pageUrl, string baseHref, string link);
    url lowercaseSchemeAndHost(url u);
    url dropDefaultPort(url u);
    url stripFragment(url u);
    url uppercasePercentEncoding(url u);
    url normalizeQuery(url u);
    url removeIndexFile(url u);
    url normalizeTrailingSlash(url u);
    url mergeWww(url u);
    string makeKey(url u);

    string registrableDomain(string host);
    bool isHomepage(normalizedUrl n);
    int normalizationVersion();
};

#endif //MCGUFFIN_URLSTANDARD_H