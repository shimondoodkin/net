#pragma once

#include <boost/asio/ssl/context.hpp>
#include <net/listener.hpp>

#include <boost/beast.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio.hpp>

#include <future>
#include <memory>

namespace net {
class client;
class server;
}

namespace wss {

class connection {
public:
    connection() = default;
    ~connection();
    
    connection(const connection&) = delete;
    connection(connection&&);
    connection& operator=(const connection&) = delete;
    connection& operator=(connection&&);

    std::future<std::string> read();
    std::future<void> write(const std::string& req);

    explicit operator bool() const;

    void close();

    class listener;
private:
    friend class net::client;
    friend class net::server;
    typedef boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>> stream_t;
    class connector;
    class disconnector;

    std::unique_ptr<stream_t> stream_;
};


class connection::connector : public std::enable_shared_from_this<connection::connector> {
    stream_t stream_;
    std::string host_, port_;
public:
    connector(boost::asio::io_context& io_ctx, boost::asio::ssl::context& ssl_ctx, const std::string& host, const std::string& port);
    std::string uri="/";
    
    void on_resolve(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results);
    void on_connect(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type::endpoint_type endpoint);
    void on_ssl_handshake(boost::beast::error_code ec);
    void on_handshake(boost::beast::error_code ec);

    std::promise<connection> result;
};


class connection::disconnector : public std::enable_shared_from_this<connection::disconnector> {
    
    std::unique_ptr<stream_t> stream_;
    std::promise<void> result;

public:
    disconnector(std::unique_ptr<stream_t>& stream);

    std::future<void> run();
    void on_shutdown(boost::beast::error_code ec);
};

class connection::listener : public net::ssl_listener {
public:
    listener(boost::asio::io_context& io_ctx, boost::asio::ssl::context& ssl_ctx, const std::string& host, uint16_t port);
    std::future<connection> accept_next();
private:
    class async_result : public std::enable_shared_from_this<async_result> {
    public:
        async_result(boost::asio::ssl::context& ssl_ctx);
        std::future<connection> run();
        void on_accept(boost::beast::error_code ec, boost::asio::ip::tcp::socket socket);
        void on_dispatch();
        void on_ssl_handshake(boost::beast::error_code ec);
        void on_websocket_accept(boost::beast::error_code ec);
        std::promise<connection> result;
    private:
        std::unique_ptr<stream_t> stream_;
        boost::asio::ssl::context& ssl_ctx_;
    };
};

}