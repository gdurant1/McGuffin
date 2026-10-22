#include <cctype>
#include <cstdlib>
#include <sstream>
#include "read.h"
#include "extractor.h"

using namespace std;

// lowercase copy of text
static string lowerText(string text) {
    for (int i = 0; i < (int)text.size(); i++) {
        text[i] = (char)tolower((unsigned char)text[i]);
    }
    return text;
}

// remove whitespace from both ends
static string trimSpaces(string text) {
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

// links inside these tags count as navigation
static bool isNavigationTag(string name) {
    return name == "nav" || name == "header" || name == "footer" || name == "aside";
}

// true if a space-separated rel value contains word
static bool relHas(string rel, string word) {
    istringstream words(lowerText(rel));
    string item;
    while (words >> item) {
        if (item == word) {
            return true;
        }
    }
    return false;
}

// split the page at '<' with readlib, skip comments, script and style, collect page links
extractionResult extractLinks(string body) {
    extractionResult result;
    istringstream stream(body);
    int navigationDepth = 0;
    bool firstPiece = true;

    while (true) {
        string piece = readline(stream, '<');
        if (stream.fail()) {
            break;
        }
        if (firstPiece) {
            firstPiece = false;
            result.hashableBody += piece;
            continue;
        }

        if (piece.rfind("!--", 0) == 0) {
            string comment = piece;
            while (comment.find("-->") == string::npos) {
                string more = readline(stream, '<');
                if (stream.fail()) {
                    break;
                }
                comment += "<" + more;
            }
            result.hashableBody += "<" + comment;
            continue;
        }

        string name = parseTagName(piece);
        if (name == "script" || name == "style") {
            while (true) {
                string inside = readline(stream, '<');
                if (stream.fail() || parseTagName(inside) == "/" + name) {
                    break;
                }
            }
            continue;
        }
        result.hashableBody += "<" + piece;

        if (isNavigationTag(name)) {
            navigationDepth++;
            continue;
        }
        if (name.size() > 1 && name[0] == '/' && isNavigationTag(name.substr(1))) {
            if (navigationDepth > 0) {
                navigationDepth--;
            }
            continue;
        }

        map<string, string> attributes;
        if (name == "a" || name == "area" || name == "iframe" || name == "link" || name == "base" || name == "meta") {
            attributes = parseAttributes(piece);
        }
        else {
            continue;
        }

        if (name == "base") {
            if (result.baseHref.empty() && attributes.count("href")) {
                result.baseHref = trimSpaces(attributes["href"]);
            }
            continue;
        }
        if (name == "meta") {
            if (lowerText(attributes["http-equiv"]) == "refresh" && result.metaRefreshUrl.empty()) {
                result.metaRefreshUrl = parseMetaRefresh(attributes["content"]);
            }
            continue;
        }
        if (name == "link" && !relHas(attributes["rel"], "canonical") && !relHas(attributes["rel"], "alternate")) {
            continue;
        }

        string attribute = "href";
        if (name == "iframe") {
            attribute = "src";
        }
        if (!attributes.count(attribute)) {
            continue;
        }
        extractedLink link;
        link.url = trimSpaces(attributes[attribute]);
        link.inNavigation = navigationDepth > 0;
        result.links.push_back(link);
    }
    return result;
}

// lowercase tag name; closing tags keep their "/"
string parseTagName(string piece) {
    string name;
    for (int i = 0; i < (int)piece.size(); i++) {
        char c = piece[i];
        if (c == '/' && i == 0) {
            name += c;
            continue;
        }
        if (isspace((unsigned char)c) || c == '>' || c == '/') {
            break;
        }
        name += (char)tolower((unsigned char)c);
    }
    return name;
}

// attributes of a tag with "double", 'single' or no quotes; first name wins
map<string, string> parseAttributes(string piece) {
    map<string, string> attributes;
    int i = 0;
    int size = (int)piece.size();
    while (i < size && !isspace((unsigned char)piece[i]) && piece[i] != '>') {
        i++;
    }

    while (i < size) {
        while (i < size && (isspace((unsigned char)piece[i]) || piece[i] == '/')) {
            i++;
        }
        if (i >= size || piece[i] == '>') {
            break;
        }

        string name;
        while (i < size && !isspace((unsigned char)piece[i]) && piece[i] != '=' && piece[i] != '>' && piece[i] != '/') {
            name += (char)tolower((unsigned char)piece[i]);
            i++;
        }
        while (i < size && isspace((unsigned char)piece[i])) {
            i++;
        }

        string value;
        if (i < size && piece[i] == '=') {
            i++;
            while (i < size && isspace((unsigned char)piece[i])) {
                i++;
            }
            if (i < size && (piece[i] == '"' || piece[i] == '\'')) {
                char quote = piece[i];
                i++;
                while (i < size && piece[i] != quote) {
                    value += piece[i];
                    i++;
                }
                i++;
            }
            else {
                while (i < size && !isspace((unsigned char)piece[i]) && piece[i] != '>') {
                    value += piece[i];
                    i++;
                }
            }
        }
        if (!name.empty() && attributes.count(name) == 0) {
            attributes[name] = decodeEntities(value);
        }
    }
    return attributes;
}

// &amp; &lt; &gt; &quot; &apos; &nbsp; and ascii numeric entities (&#38; &#x26;)
string decodeEntities(string text) {
    string out;
    int i = 0;
    while (i < (int)text.size()) {
        int semicolon = (int)text.find(';', i);
        if (text[i] != '&' || semicolon == (int)string::npos || semicolon - i > 10) {
            out += text[i];
            i++;
            continue;
        }
        string entity = text.substr(i + 1, semicolon - i - 1);
        string replacement;
        if (entity == "amp") {
            replacement = "&";
        }
        else if (entity == "lt") {
            replacement = "<";
        }
        else if (entity == "gt") {
            replacement = ">";
        }
        else if (entity == "quot") {
            replacement = "\"";
        }
        else if (entity == "apos") {
            replacement = "'";
        }
        else if (entity == "nbsp") {
            replacement = " ";
        }
        else if (entity.size() > 1 && entity[0] == '#') {
            int base = 10;
            string digits = entity.substr(1);
            if (digits[0] == 'x' || digits[0] == 'X') {
                base = 16;
                digits = digits.substr(1);
            }
            char* end = nullptr;
            long code = strtol(digits.c_str(), &end, base);
            if (!digits.empty() && *end == '\0' && code > 0 && code < 128) {
                replacement = string(1, (char)code);
            }
        }
        if (replacement.empty()) {
            out += text[i];
            i++;
        }
        else {
            out += replacement;
            i = semicolon + 1;
        }
    }
    return out;
}

// "0; url='next.html'" -> "next.html"
string parseMetaRefresh(string content) {
    int found = (int)lowerText(content).find("url");
    if (found == (int)string::npos) {
        return "";
    }
    int i = found + 3;
    while (i < (int)content.size() && (isspace((unsigned char)content[i]) || content[i] == '=')) {
        i++;
    }
    string url = trimSpaces(content.substr(i));
    if (url.size() >= 2 && (url[0] == '\'' || url[0] == '"') && url.back() == url[0]) {
        url = url.substr(1, url.size() - 2);
    }
    return url;
}