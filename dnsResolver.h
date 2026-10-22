#ifndef MCGUFFIN_DNSRESOLVER_H
#define MCGUFFIN_DNSRESOLVER_H

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <boost/asio.hpp>

using namespace std;

struct dnsInfo {
    string host;
    vector<string> ipv4;
    vector<string> ipv6;
    string canonicalName;
    bool noIpv6 = false;    // normal for many hosts, not an error
    bool failed = false;
    string error;
};

class dnsResolver {
private:
    boost::asio::ip::tcp::resolver resolver;
    unordered_map<string, dnsInfo> cache;

public:
    dnsResolver(boost::asio::io_context& context);
    void resolveHost(string host, function<void(dnsInfo)> done);
    optional<dnsInfo> getCached(string host);
};

#endif //MCGUFFIN_DNSRESOLVER_H