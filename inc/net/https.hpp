#pragma once

#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <memory>
#include <future>

namespace net {
class client;
class server;
}

namespace https {

using response_t = boost::beast::http::response<boost::beast::http::dynamic_body>;
using request_t = boost::beast::http::request<boost::beast::http::string_body>;

class connection {
public:
    connection() = default;
    connection(boost::asio::io_context& io_ctx, boost::asio::ssl::context& ssl_ctx, const std::string& sni_hostname, boost::asio::ip::tcp::resolver::results_type resolve);
    ~connection();
    connection(const connection&) = delete;
    connection(connection&&) noexcept ;
    connection& operator=(const connection&) = delete;
    connection& operator=(connection&&) noexcept;

    https::response_t send(const request_t& request);

    void close();

    explicit operator bool() const;

    std::future<void> handle_request(std::function<https::response_t(const https::request_t&)> func);

    class listener;

private:

    friend class net::client;
    friend class net::server;

    using stream_t = boost::beast::ssl_stream<boost::beast::tcp_stream>;
    class connector;

    std::unique_ptr<stream_t> stream_;
};


class connection::connector : public std::enable_shared_from_this<connection::connector> {
    
    stream_t stream_;
    std::string host_, port_;
public:
    connector(boost::asio::io_context& io_ctx, boost::asio::ssl::context& ssl_ctx, const std::string& host, const std::string& port);

    void on_resolve(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results);
    void on_connect(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type::endpoint_type endpoint);
    void on_ssl_handshake(boost::beast::error_code ec);
    void on_handshake(boost::beast::error_code ec);

    std::promise<connection> result; // NOLINT (cppcoreguidelines-non-private-member-variables-in-classes)
};


class connection::listener {
public:
    listener(boost::asio::io_context& io_ctx, boost::asio::ssl::context& ssl_ctx, const std::string& host, uint16_t port);
    std::future<connection> accept_next();
    
private:
    class async_result : public std::enable_shared_from_this<connection::listener::async_result> {
    public:
        explicit async_result(boost::asio::ssl::context& ssl_ctx);
        void on_accept(boost::beast::error_code ec, boost::asio::ip::tcp::socket socket);
        void on_dispatch();
        void on_handshake(boost::beast::error_code ec);

        std::promise<connection> result; // NOLINT (cppcoreguidelines-non-private-member-variables-in-classes)
    private:
        boost::asio::ssl::context& ssl_ctx_;
        std::unique_ptr<stream_t> stream_;
    };
    
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ssl::context& ssl_ctx_;
};

}