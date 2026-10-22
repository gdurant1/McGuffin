// each download goes through dns, connect, tls, request, response and redirects

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/x509.h>
#include "fetch.h"

using namespace std;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

// one download; every async step holds a shared_ptr so the job stays alive
class fetchJob : public enable_shared_from_this<fetchJob> {
private:
    fetch& owner;
    function<void(fetchResult)> done;
    fetchResult result;
    url current;
    bool finished = false;
    beast::flat_buffer buffer;
    http::request<http::empty_body> request;
    shared_ptr<http::response_parser<http::string_body>> parser;
    shared_ptr<beast::tcp_stream> plainStream;
    shared_ptr<beast::ssl_stream<beast::tcp_stream>> secureStream;

    void attempt(url target);
    void onResolved(dnsInfo info);
    void connectPlain(vector<tcp::endpoint> endpoints);
    void connectSecure(vector<tcp::endpoint> endpoints);
    template <typename Stream> void sendRequest(Stream& stream);
    template <typename Stream> void readResponse(Stream& stream);
    void handleResponse(bool tooLarge);
    void fail(string message, bool timedOut);
    void closeConnection();
    void finish();

public:
    fetchJob(fetch& owner, string pageUrl, function<void(fetchResult)> done);
    void start();
};

// remember the url and the callback
fetchJob::fetchJob(fetch& owner, string pageUrl, function<void(fetchResult)> done) : owner(owner), done(done) {
    this->result.requestedUrl = pageUrl;
}

// check the url, then make the first request
void fetchJob::start() {
    optional<url> parsed = parseUrl(this->result.requestedUrl);
    if (!parsed.has_value() || !isSupportedScheme(parsed.value().scheme)) {
        this->fail("not a valid http/https url", false);
        return;
    }
    this->attempt(parsed.value());
}

// start one request by looking up the host
void fetchJob::attempt(url target) {
    target.fragment = "";
    target.scheme = toLowerText(target.scheme);
    this->current = target;
    this->buffer.clear();
    auto self = this->shared_from_this();
    this->owner.resolver.resolveHost(toLowerText(target.host), [self](dnsInfo info) {
        self->onResolved(info);
    });
}

// build endpoints (ipv4 first) and connect with the right stream
void fetchJob::onResolved(dnsInfo info) {
    if (info.failed) {
        this->fail("DNS lookup failed: " + info.error, false);
        return;
    }
    int port = this->current.port;
    if (port == 0) {
        port = defaultPort(this->current.scheme);
    }
    vector<tcp::endpoint> endpoints;
    vector<string> addresses = info.ipv4;
    addresses.insert(addresses.end(), info.ipv6.begin(), info.ipv6.end());
    for (int i = 0; i < (int)addresses.size(); i++) {
        boost::system::error_code error;
        net::ip::address address = net::ip::make_address(addresses[i], error);
        if (!error) {
            endpoints.push_back(tcp::endpoint(address, (unsigned short)port));
        }
    }
    if (endpoints.empty()) {
        this->fail("no usable address for " + this->current.host, false);
        return;
    }
    if (this->current.scheme == "https") {
        this->connectSecure(endpoints);
    }
    else {
        this->connectPlain(endpoints);
    }
}

// http: connect a tcp_stream
void fetchJob::connectPlain(vector<tcp::endpoint> endpoints) {
    this->plainStream = make_shared<beast::tcp_stream>(this->owner.context);
    this->plainStream->expires_after(chrono::seconds(this->owner.timeoutSeconds));
    auto self = this->shared_from_this();
    this->plainStream->async_connect(endpoints, [self](boost::system::error_code error, tcp::endpoint) {
        if (error) {
            self->fail("connect: " + error.message(), error == beast::error::timeout);
            return;
        }
        self->sendRequest(*self->plainStream);
    });
}

// https: ssl_stream over tcp_stream with sni; a bad certificate is recorded, not fatal
void fetchJob::connectSecure(vector<tcp::endpoint> endpoints) {
    this->secureStream = make_shared<beast::ssl_stream<beast::tcp_stream>>(this->owner.context, this->owner.sslContext);
    string host = this->current.host;
    SSL_set_tlsext_host_name(this->secureStream->native_handle(), host.c_str());
    this->secureStream->set_verify_mode(ssl::verify_peer);
    this->secureStream->set_verify_callback([this, host](bool preverified, ssl::verify_context& context) {
        if (!ssl::host_name_verification(host)(preverified, context) && this->result.certificateOk) {
            this->result.certificateOk = false;
            int code = X509_STORE_CTX_get_error(context.native_handle());
            this->result.certificateError = "certificate is not for " + host;
            if (code != X509_V_OK) {
                this->result.certificateError = X509_verify_cert_error_string(code);
            }
        }
        return true;
    });

    beast::get_lowest_layer(*this->secureStream).expires_after(chrono::seconds(this->owner.timeoutSeconds));
    auto self = this->shared_from_this();
    beast::get_lowest_layer(*this->secureStream).async_connect(endpoints,
        [self](boost::system::error_code error, tcp::endpoint) {
            if (error) {
                self->fail("connect: " + error.message(), error == beast::error::timeout);
                return;
            }
            self->secureStream->async_handshake(ssl::stream_base::client, [self](boost::system::error_code error) {
                if (error) {
                    self->fail("TLS handshake: " + error.message(), error == beast::error::timeout);
                    return;
                }
                self->sendRequest(*self->secureStream);
            });
        });
}

// send the get request (no compression)
template <typename Stream>
void fetchJob::sendRequest(Stream& stream) {
    string target = this->current.path;
    if (!this->current.query.empty()) {
        target += "?" + this->current.query;
    }
    string hostHeader = this->current.host;
    if (this->current.port != 0) {
        hostHeader += ":" + to_string(this->current.port);
    }
    this->request = http::request<http::empty_body>(http::verb::get, target, 11);
    this->request.set(http::field::host, hostHeader);
    this->request.set(http::field::user_agent, "McGuffinCrawler/1.0 (+https://github.com/gdurant1/McGuffin/projects)");
    this->request.set(http::field::accept, "text/html,application/xhtml+xml;q=0.9,*/*;q=0.8");
    this->request.set(http::field::connection, "close");

    beast::get_lowest_layer(stream).expires_after(chrono::seconds(this->owner.timeoutSeconds));
    auto self = this->shared_from_this();
    http::async_write(stream, this->request, [self, &stream](boost::system::error_code error, size_t) {
        if (error) {
            self->fail("sending request: " + error.message(), error == beast::error::timeout);
            return;
        }
        self->readResponse(stream);
    });
}

// read the response; the body stops at the size limit
template <typename Stream>
void fetchJob::readResponse(Stream& stream) {
    this->parser = make_shared<http::response_parser<http::string_body>>();
    this->parser->body_limit((uint64_t)this->owner.maxBodyBytes);
    beast::get_lowest_layer(stream).expires_after(chrono::seconds(this->owner.timeoutSeconds));
    auto self = this->shared_from_this();
    http::async_read(stream, this->buffer, *this->parser, [self](boost::system::error_code error, size_t) {
        bool tooLarge = error == http::error::body_limit && self->parser->is_header_done();
        bool truncatedButDone = error == ssl::error::stream_truncated && self->parser->is_done();
        if (error && !tooLarge && !truncatedButDone) {
            self->fail("reading response: " + error.message(), error == beast::error::timeout);
            return;
        }
        self->handleResponse(tooLarge);
    });
}

// follow a redirect, or report the final answer
void fetchJob::handleResponse(bool tooLarge) {
    http::response<http::string_body>& response = this->parser->get();
    int status = (int)response.result_int();
    string location = string(response[http::field::location]);
    string retryAfter = trimText(string(response[http::field::retry_after]));
    this->result.statusCode = status;
    this->result.contentType = string(response[http::field::content_type]);
    this->result.retryAfterSeconds = -1;
    if (!retryAfter.empty() && retryAfter.size() <= 6 && retryAfter.find_first_not_of("0123456789") == string::npos) {
        this->result.retryAfterSeconds = stoi(retryAfter);
    }
    this->closeConnection();

    bool isRedirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    if (isRedirect && !location.empty()) {
        optional<url> next = this->owner.standard.resolveRelative(this->current.toString(), "", location);
        if (!next.has_value() || !isSupportedScheme(next.value().scheme)) {
            this->fail("redirect to an unusable location: " + location, false);
            return;
        }
        this->result.redirectChain.push_back(this->current.toString());
        if ((int)this->result.redirectChain.size() > this->owner.maxRedirects) {
            this->fail("more than " + to_string(this->owner.maxRedirects) + " redirects", false);
            return;
        }
        this->attempt(next.value());
        return;
    }

    this->result.finalUrl = this->current.toString();
    this->result.tooLarge = tooLarge;
    if (!tooLarge) {
        this->result.body = response.body();
    }
    string type = toLowerText(this->result.contentType);
    this->result.isHtml = type.empty() || startsWithText(type, "text/html") || startsWithText(type, "application/xhtml+xml");
    this->finish();
}

// report a download with no usable answer
void fetchJob::fail(string message, bool timedOut) {
    this->result.failed = true;
    this->result.timedOut = timedOut;
    this->result.error = message;
    if (this->result.finalUrl.empty()) {
        this->result.finalUrl = this->current.toString();
    }
    this->closeConnection();
    this->finish();
}

// close the connection; errors don't matter here
void fetchJob::closeConnection() {
    boost::system::error_code ignored;
    if (this->plainStream) {
        this->plainStream->socket().close(ignored);
        this->plainStream.reset();
    }
    if (this->secureStream) {
        beast::get_lowest_layer(*this->secureStream).socket().close(ignored);
        this->secureStream.reset();
    }
}

// hand the result to the crawler once
void fetchJob::finish() {
    if (!this->finished) {
        this->finished = true;
        this->done(this->result);
    }
}

// load trusted certificates: system store, plus cacert.pem when present
fetch::fetch(boost::asio::io_context& context, dnsResolver& resolver, int timeoutSeconds, int maxBodyBytes, int maxRedirects)
    : context(context), resolver(resolver), sslContext(ssl::context::tls_client),
      timeoutSeconds(timeoutSeconds), maxBodyBytes(maxBodyBytes), maxRedirects(maxRedirects) {
    boost::system::error_code systemError;
    boost::system::error_code fileError(boost::system::errc::no_such_file_or_directory, boost::system::generic_category());
    this->sslContext.set_default_verify_paths(systemError);
    if (filesystem::exists("cacert.pem")) {
        this->sslContext.load_verify_file("cacert.pem", fileError);
    }
    if (systemError && fileError) {
        throw runtime_error("no trusted certificates: system store failed and cacert.pem could not be loaded");
    }
}

// start a download; done is called once with the result
void fetch::download(string pageUrl, function<void(fetchResult)> done) {
    make_shared<fetchJob>(*this, pageUrl, done)->start();
}