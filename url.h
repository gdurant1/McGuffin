#ifndef MCGUFFIN_URL_H
#define MCGUFFIN_URL_H

#include <string>
#include <optional>
#include <cctype>

using namespace std;

struct url {
    string scheme;     // "http" or "https", empty for scheme-relative links (//host/path)
    string host;       // hostname, or [address] for IPv6 literals
    int port;          // 0 means no port was written (use the scheme's default)
    string path;       // always starts with "/"
    string query;      // text after "?", without the "?"
    string fragment;   // text after "#", without the "#"
    string original;   // trimmed text the url was parsed from (kept for saving and re-normalizing)

    // Default constructor
    url() {
        this->port = 0;
    }

    // constructor
    url(string scheme, string host, int port, string path,
        string query, string fragment, string original) {
        this->scheme = scheme;
        this->host = host;
        this->port = port;
        this->path = path;
        this->query = query;
        this->fragment = fragment;
        this->original = original;
    }

    // build full url text from parts
    string toString() const {
        string result;
        //scheme followed by "://", or just "//" for scheme-relative links
        if (this->scheme.empty()) {
            result += "//";
        }
        else {
            result += this->scheme + "://";
        }
        //hostname
        result += this->host;
        //only add ":port" when a port was written in the original url
        if (this->port != 0) {
            result += ":" + to_string(this->port);
        }
        //path always present, at least "/"
        result += this->path;
        //add "?query" only if there is a query
        if (!this->query.empty()) {
            result += "?" + this->query;
        }
        //add "#fragment" only if there is a fragment
        if (!this->fragment.empty()) {
            result += "#" + this->fragment;
        }
        return result;
    }
};

// HELPER FUNCTIONS
// Remove spaces, tabs and newlines from both ends
inline string trimText(string text) {
    int start = 0;
    int end = (int)text.size();
    //move start forward past leading whitespace
    while (start < end && isspace((unsigned char)text[start])) {
        start++;
    }
    //move end backward past trailing whitespace
    while (end > start && isspace((unsigned char)text[end - 1])) {
        end--;
    }
    //keep only the part between start and end
    return text.substr(start, end - start);
}

// Return a lowercase copy of text
inline string toLowerText(string text) {
    //lowercase each character in place (text is already a copy)
    for (int i = 0; i < (int)text.size(); i++) {
        text[i] = (char)tolower((unsigned char)text[i]);
    }
    return text;
}

// True if text starts with prefix
inline bool startsWithText(string text, string prefix) {
    //text must be at least as long as prefix, and its first characters must match
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}


// True only for http and https (the crawler fetches nothing else)
// Case does not matter: "HTTPS" is supported
inline bool isSupportedScheme(string scheme) {
    scheme = toLowerText(scheme);
    return scheme == "http" || scheme == "https";
}

// Default port for scheme
// 80 for http, 443 for https, 0 for anything else
inline int defaultPort(string scheme) {
    scheme = toLowerText(scheme);
    if (scheme == "http") {
        return 80;
    }
    if (scheme == "https") {
        return 443;
    }
    return 0;
}

// LINK CHECKS
// True for links the crawler always skips:
// Case and surrounding spaces do not matter
inline bool isSkippableLink(string text) {
    text = toLowerText(trimText(text));
    //empty href="" links to the same page
    if (text.empty()) {
        return true;
    }
    //not web pages
    return startsWithText(text, "mailto:") || startsWithText(text, "tel:")
        || startsWithText(text, "javascript:") || startsWithText(text, "data:");
}

// True for links that are only "#something"
inline bool isFragmentOnly(string text) {
    text = trimText(text);
    return !text.empty() && text[0] == '#';
}

// PARSING
// Turns url text into a url struct.
// True if text is a valid scheme name
inline bool isValidScheme(string text) {
    //must not be empty and must start with a letter
    if (text.empty() || !isalpha((unsigned char)text[0])) {
        return false;
    }
    //check every remaining character
    for (int i = 1; i < (int)text.size(); i++) {
        char c = text[i];
        if (!isalnum((unsigned char)c) && c != '+' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

// Convert port text to a number
// Returns nullopt if the text is not a valid port (digits only, 1-65535)
inline optional<int> parsePort(string text) {
    //empty, or more than 5 digits can't be a valid port (max 65535)
    if (text.empty() || text.size() > 5) {
        return nullopt;
    }
    //every character must be a digit
    for (int i = 0; i < (int)text.size(); i++) {
        if (!isdigit((unsigned char)text[i])) {
            return nullopt;
        }
    }
    //safe to convert now, then check the range
    int port = stoi(text);
    if (port < 1 || port > 65535) {
        return nullopt;
    }
    return port;
}

// Split an absolute or scheme-relative url into its parts
// Nothing is normalized here: case, ports, slashes stay exactly as written.
inline optional<url> parseUrl(string text) {
    url result;

    //remove surrounding whitespace and remember the original text
    text = trimText(text);
    result.original = text;
    if (text.empty()) {
        return nullopt;
    }

    // scheme
    //"rest" will hold everything after the scheme and "//"
    string rest;
    int schemeEnd = (int)text.find("://");
    //absolute url: scheme comes before "://"
    if (schemeEnd > 0) {
        result.scheme = text.substr(0, schemeEnd);
        if (!isValidScheme(result.scheme)) {
            return nullopt;
        }
        rest = text.substr(schemeEnd + 3);
    }
    //scheme-relative url: starts with "//", scheme filled in later from the page
    else if (startsWithText(text, "//")) {
        rest = text.substr(2);
    }
    //relative link or not a url
    else {
        return nullopt;
    }

    // split authority (host + port) from the rest
    //authority ends at the first /, ? or #
    int authorityEnd = (int)rest.find_first_of("/?#");
    string authority;
    string remainder;
    if (authorityEnd == (int)string::npos) {
        //no path, query or fragment: everything is authority
        authority = rest;
    }
    else {
        authority = rest.substr(0, authorityEnd);
        remainder = rest.substr(authorityEnd);
    }

    // drop "user:password@" if present
    int atSign = (int)authority.rfind('@');
    if (atSign != (int)string::npos) {
        authority = authority.substr(atSign + 1);
    }

    // host and port
    string portText;
    bool hasPort = false;
    //IPv6 literal like [::1]:8080 - host includes the brackets
    if (startsWithText(authority, "[")) {
        int closeBracket = (int)authority.find(']');
        //missing "]" means a broken address
        if (closeBracket == (int)string::npos) {
            return nullopt;
        }
        result.host = authority.substr(0, closeBracket + 1);
        //after "]" there may be ":port"
        string afterHost = authority.substr(closeBracket + 1);
        if (startsWithText(afterHost, ":")) {
            hasPort = true;
            portText = afterHost.substr(1);
        }
        //anything else after "]" is invalid
        else if (!afterHost.empty()) {
            return nullopt;
        }
    }
    //normal hostname: port is after the last ":"
    else {
        int colon = (int)authority.rfind(':');
        if (colon == (int)string::npos) {
            result.host = authority;
        }
        else {
            result.host = authority.substr(0, colon);
            hasPort = true;
            portText = authority.substr(colon + 1);
        }
    }
    //a url must have a host (rejects "https:///a")
    if (result.host.empty()) {
        return nullopt;
    }
    //convert port text; "host:" with nothing after it keeps the default (port = 0)
    if (hasPort && !portText.empty()) {
        optional<int> port = parsePort(portText);
        if (!port.has_value()) {
            return nullopt;
        }
        result.port = port.value();
    }

    // fragment (after the first "#")
    //removed first, because a "?" inside a fragment is not a query
    int hashSign = (int)remainder.find('#');
    if (hashSign != (int)string::npos) {
        result.fragment = remainder.substr(hashSign + 1);
        remainder = remainder.substr(0, hashSign);
    }

    // query (after the first "?")
    int questionMark = (int)remainder.find('?');
    if (questionMark != (int)string::npos) {
        result.query = remainder.substr(questionMark + 1);
        remainder = remainder.substr(0, questionMark);
    }

    // path (whatever is left)
    //missing path becomes "/" so every url has one
    if (remainder.empty()) {
        result.path = "/";
    }
    else {
        result.path = remainder;
    }

    return result;
}


#endif //MCGUFFIN_URL_H
