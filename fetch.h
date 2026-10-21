#ifndef MCGUFFIN_FETCH_H
#define MCGUFFIN_FETCH_H

#include <functional>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "dnsResolver.h"
#include "urlStandard.h"

using namespace std;

struct fetchResult {
    string requestedUrl;
    string finalUrl;                // url that answered after redirects
    vector<string> redirectChain;
    int statusCode = 0;
    string contentType;
    string body;                    // empty when too large
    bool isHtml = false;
    bool tooLarge = false;
    bool certificateOk = true;
    string certificateError;
    int retryAfterSeconds = -1;     // -1 = no retry-after header
    bool failed = false;            // no usable answer
    bool timedOut = false;
    string error;
};

class fetchJob;

class fetch {
private:
    friend class fetchJob;
    boost::asio::io_context& context;
    dnsResolver& resolver;
    boost::asio::ssl::context sslContext;
    urlStandard standard;
    int timeoutSeconds;
    int maxBodyBytes;
    int maxRedirects;

public:
    fetch(boost::asio::io_context& context, dnsResolver& resolver,
          int timeoutSeconds, int maxBodyBytes, int maxRedirects);
    void download(string pageUrl, function<void(fetchResult)> done);
};

#endif //MCGUFFIN_FETCH_H