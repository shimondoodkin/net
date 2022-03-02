#include <boost/asio/executor_work_guard.hpp>
#include <boost/system/detail/errc.hpp>
#include <net/client.hpp>
#include <net/cert_utils.hpp>

#include <boost/beast.hpp>
#include <boost/asio.hpp>

#include <string_view>
#include <thread>
#include <iostream>

namespace net {

client::client():
        work_guard_(boost::asio::make_work_guard(io_ctx_)), // prevents io_ctx_ from stopping when there is no work to do
        ssl_ctx_(boost::asio::ssl::context::tlsv13_client),
        resolver_(boost::asio::make_strand(io_ctx_)),
        update_resolve_timer_(io_ctx_, update_resolve_timer_interval_)
         {
#ifdef _WIN32
    import_openssl_certificates(ssl_ctx_.native_handle());
#endif
    ssl_ctx_.set_verify_mode(boost::asio::ssl::verify_peer);
    ssl_ctx_.set_default_verify_paths();
    
    bg_ = std::thread([this]() -> void {
        io_ctx_.run();
    });
    
    // Posts the timer event
    update_resolve_timer_.async_wait([this](const boost::system::error_code& e){this->update_resolve(e);});
}

client::~client() {
    this->update_resolve_timer_destroying=true;
    this->update_resolve_timer_.expires_from_now(boost::posix_time::seconds(0));
    this->update_resolve_timer_.cancel();
    io_ctx_.stop();
    bg_.join();
}

// std::string datetostring(DateTime t)
// {
//  std::stringstream ss;
//  ss << static_cast<int>(t.time_of_day().hours()) << ":" << t.time_of_day().minutes() << ":" << t.time_of_day().seconds();
//  return ss.str();
// }
 
void client::update_resolve(const boost::system::error_code& ec) {
    if (!ec) { return; }
    if (this->update_resolve_timer_destroying) { return; }

    // std::cout << "Client::update_resolve" << std::endl;
    DateTime now_sooner = boost::posix_time::second_clock::local_time() - boost::posix_time::seconds(this->resolver_cache_expiers_soon_seconds_);
    for(auto &i:this->resolver_cache_.map)
    {
        // std::cout << "Client::update_resolve at "<< i.first << std::endl;
        // std::cout << "Client::update_resolve expires "<< datetostring(i.second.expires) << std::endl;
        // std::cout << "Client::update_resolve lastused "<< datetostring(i.second.lastuse) << std::endl;

        if(i.second.expires <= now_sooner)
        {
         const resolver_cache_value_t resolvedip=resolver_.resolve(i.second.host,i.second.port );
         i.second.resolved=resolvedip;
         i.second.expires=boost::posix_time::second_clock::local_time() + boost::posix_time::seconds(this->resolver_cache_expiers_seconds_);
         // std::cout << "Client::update_resolve update expires to  "<< datetostring(i.second.expires) << std::endl;
        }
    }

    // Reschedule the timer for 1 second in the future:
    update_resolve_timer_.expires_at(update_resolve_timer_.expires_at() + update_resolve_timer_interval_);
    // Posts the timer event
    update_resolve_timer_.async_wait([this](const boost::system::error_code& e){this->update_resolve(e);});
}

bool ResolverCache::get(const resolver_cache_key_t &key, resolver_cache_value_t& value) {
    std::lock_guard<std::mutex> lock(this->resolver_cache_mutex_);
    const auto &i = this->map.find(key);
    if (i != this->map.end()) {
        DateTime now = boost::posix_time::second_clock::local_time();
        i->second.lastuse=now;
        value = i->second.resolved;
        return i->second.expires >= now;
    }
    return false;
}

void ResolverCache::put(const resolver_cache_key_t &key, resolver_cache_value_t& value, const std::string& host, const std::string& port, int expiers_seconds ) {
    std::lock_guard<std::mutex> lock(this->resolver_cache_mutex_);
    DateTime now = boost::posix_time::second_clock::local_time();
    this->map[key]={.resolved=value, .expires=now + boost::posix_time::seconds(expiers_seconds), .lastuse=now, .host=host, .port=port};
}

boost::asio::ssl::context& client::ssl_context() {
    return ssl_ctx_;
}

http::connection client::http(const std::string& host, const std::string& port) {
    boost::asio::ip::tcp::resolver::results_type resolvedip;
    std::string key=host+":"+port;
    if(!this->resolver_cache_.get( key, resolvedip ))
    {
        resolvedip=resolver_.resolve(host, port);
        this->resolver_cache_.put(key,resolvedip, host, port, this->resolver_cache_expiers_seconds_);
    }
    http::connection connection(io_ctx_, host, resolvedip); 
    return connection;
}

std::future<ws::connection> client::ws(const std::string& host, const std::string& port, const std::string& uri) {
    auto connector = std::make_shared<ws::connection::connector>(io_ctx_, host, port);

    boost::asio::ip::tcp::resolver::results_type resolvedip;
    std::string key=host+":"+port;
    if(!this->resolver_cache_.get( key, resolvedip ))
    {
        resolver_.async_resolve( host, port, [&](boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results) {
            if (ec) {
                connector->result.set_exception(std::make_exception_ptr(std::runtime_error(ec.message())));
                return;
            };
            this->resolver_cache_.put(key,results, host, port, this->resolver_cache_expiers_seconds_);
            connector->on_resolve(ec,results);
        });
        return connector->result.get_future();
    }
    auto  success = boost::system::errc::make_error_code(boost::system::errc::success);
    connector->on_resolve( success ,resolvedip);

    return connector->result.get_future();
}

https::connection client::https(const std::string& host, const std::string& port) {
    boost::asio::ip::tcp::resolver::results_type resolvedip;
    std::string key=host+":"+port;
    if(!this->resolver_cache_.get( key, resolvedip ))
    {
        resolvedip=resolver_.resolve(host, port);
        this->resolver_cache_.put(key,resolvedip, host, port, this->resolver_cache_expiers_seconds_);
    }
    return https::connection(io_ctx_, ssl_ctx_, host, resolvedip );
}

std::future<wss::connection> client::wss(const std::string& host, const std::string& port, const std::string& uri) {
    auto connector = std::make_shared<wss::connection::connector>(io_ctx_, ssl_ctx_, host, port);

    boost::asio::ip::tcp::resolver::results_type resolvedip;
    std::string key=host+":"+port;
    if(!this->resolver_cache_.get( key, resolvedip ))
    {
        resolver_.async_resolve( host, port, [&](boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results) {
            if (ec) {
                connector->result.set_exception(std::make_exception_ptr(std::runtime_error(ec.message())));
                return;
            };
            this->resolver_cache_.put(key,results, host, port, this->resolver_cache_expiers_seconds_);
            connector->on_resolve(ec,results);
        });
        return connector->result.get_future();
    }
    auto  success = boost::system::errc::make_error_code(boost::system::errc::success);
    connector->on_resolve( success ,resolvedip);

    return connector->result.get_future();
}

}