#ifndef MCGUFFIN_URL_H
#define MCGUFFIN_URL_H

#include <string>
#include <optional>
#include <cctype>

using namespace std;

struct url {
    string scheme;
    string host;
    int port;
    string path;
    string query;
    string fragment;
    string original;

    url(){ this->port = 0;}

    url(string scheme, string host, int port, string path, string query, string fragment, string original) {
        this->scheme = scheme;
        this->host = host;
        this->port = port;
        this->path = path;
        this->query = query;
        this->fragment = fragment;
        this->original = original;

        string toString() const {
            string result;

            if (this->scheme.empty()) { result += "//"; }
            else {result += this->scheme + "://"; }
        };
    }

};

#endif //MCGUFFIN_URL_H
