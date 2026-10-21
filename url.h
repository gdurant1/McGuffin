#ifndef MCGUFFIN_URL_H
#define MCGUFFIN_URL_H

#include <cctype>
#include <optional>
#include <string>

using namespace std;

struct url {
    string scheme;
    string host;
    int port = 0;
    string path;
    string query;
    string fragment;
    string original;

    // rebuild the url text from its parts
    string toString() const {
        string result;
        if (this->scheme.empty()) {
            result += "//";
        }
        else {
            result += this->scheme + "://";
        }
        result += this->host;
        if (this->port != 0) {
            result += ":" + to_string(this->port);
        }
        result += this->path;
        if (!this->query.empty()) {
            result += "?" + this->query;
        }
        if (!this->fragment.empty()) {
            result += "#" + this->fragment;
        }
        return result;
    }
};

// remove whitespace from both ends
inline string trimText(string text) {
    int start = 0;
    int end = (int)text.size();
    while (start < end && isspace((unsigned char)text[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char)text[end - 1])) {
        end--;
    }
    return text.substr(start, end - start);
}

// lowercase copy of text
inline string toLowerText(string text) {
    for (int i = 0; i < (int)text.size(); i++) {
        text[i] = (char)tolower((unsigned char)text[i]);
    }
    return text;
}

// true if text starts with prefix
inline bool startsWithText(string text, string prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// only http and https are crawled
inline bool isSupportedScheme(string scheme) {
    scheme = toLowerText(scheme);
    return scheme == "http" || scheme == "https";
}

// 80 for http, 443 for https
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

// empty, mailto:, tel:, javascript: and data: links are never pages
inline bool isSkippableLink(string text) {
    text = toLowerText(trimText(text));
    if (text.empty()) {
        return true;
    }
    return startsWithText(text, "mailto:") || startsWithText(text, "tel:")
        || startsWithText(text, "javascript:") || startsWithText(text, "data:");
}

// "#top" points to the same page
inline bool isFragmentOnly(string text) {
    text = trimText(text);
    return !text.empty() && text[0] == '#';
}

// scheme: a letter, then letters, digits, +, - or .
inline bool isValidScheme(string text) {
    if (text.empty() || !isalpha((unsigned char)text[0])) {
        return false;
    }
    for (int i = 1; i < (int)text.size(); i++) {
        char c = text[i];
        if (!isalnum((unsigned char)c) && c != '+' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

// port text to a number, nullopt unless 1-65535
inline optional<int> parsePort(string text) {
    if (text.empty() || text.size() > 5) {
        return nullopt;
    }
    for (int i = 0; i < (int)text.size(); i++) {
        if (!isdigit((unsigned char)text[i])) {
            return nullopt;
        }
    }
    int port = stoi(text);
    if (port < 1 || port > 65535) {
        return nullopt;
    }
    return port;
}

// split an absolute or //host url into parts
// nullopt for relative links, bad urls and ipv6 literals
inline optional<url> parseUrl(string text) {
    url result;
    text = trimText(text);
    result.original = text;

    string rest;
    int schemeEnd = (int)text.find("://");
    if (schemeEnd > 0) {
        result.scheme = text.substr(0, schemeEnd);
        if (!isValidScheme(result.scheme)) {
            return nullopt;
        }
        rest = text.substr(schemeEnd + 3);
    }
    else if (startsWithText(text, "//")) {
        rest = text.substr(2);
    }
    else {
        return nullopt;
    }

    int authorityEnd = (int)rest.find_first_of("/?#");
    string authority = rest;
    string remainder;
    if (authorityEnd != (int)string::npos) {
        authority = rest.substr(0, authorityEnd);
        remainder = rest.substr(authorityEnd);
    }
    int atSign = (int)authority.rfind('@');
    if (atSign != (int)string::npos) {
        authority = authority.substr(atSign + 1);
    }
    if (authority.find('[') != string::npos) {
        return nullopt;
    }

    int colon = (int)authority.rfind(':');
    result.host = authority;
    if (colon != (int)string::npos) {
        result.host = authority.substr(0, colon);
        string portText = authority.substr(colon + 1);
        if (!portText.empty()) {
            optional<int> port = parsePort(portText);
            if (!port.has_value()) {
                return nullopt;
            }
            result.port = port.value();
        }
    }
    if (result.host.empty()) {
        return nullopt;
    }

    int hashSign = (int)remainder.find('#');
    if (hashSign != (int)string::npos) {
        result.fragment = remainder.substr(hashSign + 1);
        remainder = remainder.substr(0, hashSign);
    }
    int questionMark = (int)remainder.find('?');
    if (questionMark != (int)string::npos) {
        result.query = remainder.substr(questionMark + 1);
        remainder = remainder.substr(0, questionMark);
    }
    result.path = remainder;
    if (result.path.empty()) {
        result.path = "/";
    }
    return result;
}

#endif //MCGUFFIN_URL_H