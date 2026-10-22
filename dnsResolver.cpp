#include "dnsResolver.h"

using namespace std;

// the resolver runs on the crawler's io_context
dnsResolver::dnsResolver(boost::asio::io_context& context) : resolver(context) {}

// look up a host (cache first) and pass the result to done
// failed lookups aren't cached, so a later page can try again
void dnsResolver::resolveHost(string host, function<void(dnsInfo)> done) {
    optional<dnsInfo> cached = this->getCached(host);
    if (cached.has_value()) {
        done(cached.value());
        return;
    }
    this->resolver.async_resolve(host, "80", boost::asio::ip::resolver_base::canonical_name,
        [this, host, done](const boost::system::error_code& error,
                           boost::asio::ip::tcp::resolver::results_type results) {
            dnsInfo info;
            info.host = host;
            if (error) {
                info.failed = true;
                info.error = error.message();
                done(info);
                return;
            }
            for (const auto& entry : results) {
                boost::asio::ip::address address = entry.endpoint().address();
                if (address.is_v4()) {
                    info.ipv4.push_back(address.to_string());
                }
                else {
                    info.ipv6.push_back(address.to_string());
                }
                if (info.canonicalName.empty()) {
                    info.canonicalName = entry.host_name();
                }
            }
            info.noIpv6 = info.ipv6.empty();
            this->cache[host] = info;
            done(info);
        });
}

// cached result, or nullopt if not looked up yet
optional<dnsInfo> dnsResolver::getCached(string host) {
    auto found = this->cache.find(host);
    if (found == this->cache.end()) {
        return nullopt;
    }
    return found->second;
}