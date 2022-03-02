#pragma once

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <net/http.hpp>
#include <net/https.hpp>
#include <net/ws.hpp>
#include <net/wss.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>



namespace net {

    using DateTime = boost::posix_time::ptime;
    using resolver_cache_key_t = std::string;
    using resolver_cache_value_t =  boost::asio::ip::tcp::resolver::results_type;

    struct resolver_cache_entery_t {
        resolver_cache_value_t resolved;
        net::DateTime expires;
        net::DateTime lastuse;
        std::string host;
        std::string port;
    } __attribute__((aligned(128)));
    using resolver_cache_t = std::unordered_map<resolver_cache_key_t, resolver_cache_entery_t>;

    struct ResolverCache {
        bool get(const resolver_cache_key_t &key, resolver_cache_value_t&value) ;
        void put(const resolver_cache_key_t &key, resolver_cache_value_t& value, std::string host, std::string port, int expiers_soon_seconds );
        resolver_cache_t map{};
        private:
        std::mutex resolver_cache_mutex_{};
    } __attribute__((aligned(128)));

class client {
public:
    client();
    ~client();

    boost::asio::ssl::context& ssl_context();

    http::connection http(const std::string& host, const std::string& port);
    std::future<ws::connection> ws(const std::string& host, const std::string& port, const std::string& uri);

    https::connection https(const std::string& host, const std::string& port);
    std::future<wss::connection> wss(const std::string& host, const std::string& port, const std::string& uri);
    void update_resolve(const boost::system::error_code& /*e*/);


private:
    boost::asio::io_context io_ctx_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    boost::asio::ssl::context ssl_ctx_;
    boost::asio::ip::tcp::resolver resolver_;
    ResolverCache resolver_cache_;
    std::thread bg_;

    int resolver_cache_expiers_seconds_{30}; 
    int resolver_cache_expiers_soon_seconds_{10}; 
    boost::posix_time::seconds update_resolve_timer_interval_{5};
    boost::asio::deadline_timer update_resolve_timer_;
    bool update_resolve_timer_destroying{false};

};

}