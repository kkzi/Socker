#pragma once

#include <Logger/Logger.h>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <future>
#include <utility>
#include <unordered_map>


namespace http
{
    namespace chrono = std::chrono;         // from <chrono>
    namespace beast = boost::beast;         // from <boost/beast.hpp>
    namespace bh = beast::http;             // from <boost/beast/http.hpp>
    namespace net = boost::asio;            // from <boost/asio.hpp>
    using tcp = net::ip::tcp;               // from <boost/asio/ip/tcp.hpp>

    // headers key via boost/beast/http/impl/field.ipp field_table
    using headers = std::unordered_map<std::string, std::string>;

    static const char* DEFAULT_PORT = "80";
    static const char* DEFAULT_RESOURCE = "/";
    static const int VERSION = 11;

    struct Url
    {
        std::string protocol;
        std::string host;
        std::string port{ DEFAULT_PORT };
        std::string resource{ DEFAULT_RESOURCE };

        [[nodiscard]] bool isValid() const
        {
            return (protocol == "http" || protocol == "https") && !host.empty();
        }

        [[nodiscard]] std::string toString() const
        {
            return protocol + "://" + host + ":" + port + resource;
        }
    };

    struct Request
    {
        Url url;
        headers head;
        std::string body;
        int timeout{ 0 }; // <=0 means no timeout, >0 timeout seconds
    };

    struct Reply
    {
        int code{ (int)bh::status::ok };
        headers head;
        std::string body;
        bh::response<bh::string_body> raw;

        Reply() = default;

        explicit Reply(bh::response<bh::string_body> raw_)
            : raw(std::move(raw_))
        {
            code = (int)raw.result_int();
            for (const auto& it : raw)
            {
                head[it.name_string().to_string()] = it.value().to_string();
            }
            body = raw.body();
        }
    };

    using ReplyHandler = std::function<void(const Reply& reply)>;


    static Request makeRequest(const std::string& url, const headers& head, const std::string& body = "", int timeout = 0)
    {
        std::string fixed;
        if (boost::starts_with(url, "http"))
        {
            fixed = url;
        }
        else
        {
            fixed += "http";
            if (boost::starts_with(url, ":"))
            {
                fixed += url;
            }
            else
            {
                fixed += ("://" + url);
            }
        }

        std::vector<std::string> token;
        boost::split(token, fixed, boost::is_any_of(":/"));

#ifndef GET_ITEM
#define GET_ITEM(pos) token.size() > (pos) ? token.at(pos) : ""
        bool hasPort = fixed.find_first_of(':', 9) != std::string::npos;
        std::string protocol = GET_ITEM(0);
        std::string host = GET_ITEM(3);
        std::string port = hasPort ? GET_ITEM(4) : "";
#undef  GET_ITEM
#endif

        if (port.empty())
        {
            port = DEFAULT_PORT;
        }

        auto respos = fixed.find_first_of('/', 9);
        std::string res = respos == std::string::npos ? "" : fixed.substr(respos);
        if (!boost::starts_with(res, "/"))
        {
            res = "/" + res;
        }

        return Request{ Url{ protocol, host, port, res }, head, body, timeout };
    }

    class Client final
    {
    public:
        explicit Client(int threadCount = 1, int timeout = 0)
            : guard_(net::make_work_guard(io_))
        {
            for (auto i = 0; i < threadCount; ++i)
            {
                workers_.emplace_back([=]
                    {
                        io_.run();
                    }
                );
            }

            setTimeout(timeout);
        }

        ~Client()
        {
            drop();
        }

    public:
        void setTimeout(int seconds)
        {
            timeout_ = seconds;
        }

        void get(const Request& req, const ReplyHandler& func)
        {
            net::spawn(io_, std::bind(&Client::doSend, this, bh::verb::get, req, func, std::placeholders::_1));
        }

        void get(const std::string& url, const ReplyHandler& func, int timeout = 0)
        {
            return get(makeRequest(url, headers(), "", timeout), func);
        }

        void get(const std::string& url, const headers& head, const ReplyHandler& func, int timeout = 0)
        {
            return get(makeRequest(url, head, "", timeout), func);
        }

        void post(const Request& req, const ReplyHandler& func)
        {
            net::spawn(io_, std::bind(&Client::doSend, this, bh::verb::post, req, func, std::placeholders::_1));
        }

        void post(const std::string& url, const headers& head, const std::string& body, const ReplyHandler& func, int timeout = 0)
        {
            return post(makeRequest(url, head, body, timeout), func);
        }

        void put(const Request& req, const ReplyHandler& func)
        {
            net::spawn(io_, std::bind(&Client::doSend, this, bh::verb::put, req, func, std::placeholders::_1));
        }

        void put(const std::string& url, const headers& head, const std::string& body, const ReplyHandler& func, int timeout = 0)
        {
            return put(makeRequest(url, head, body, timeout), func);
        }

        void del(const Request& req, const ReplyHandler& func)
        {
            net::spawn(io_, std::bind(&Client::doSend, this, bh::verb::delete_, req, func, std::placeholders::_1));
        }

        void del(const std::string& url, const headers& head, const std::string& body, const ReplyHandler& func, int timeout = 0)
        {
            return del(makeRequest(url, head, body, timeout), func);
        }

        void send(bh::verb verb, const Request& req, const ReplyHandler& func)
        {
            net::spawn(io_, std::bind(&Client::doSend, this, verb, req, func, std::placeholders::_1));
        }

        Reply syncGet(const Request& req)
        {
            return syncSend(bh::verb::get, req);
        }

        Reply syncGet(const std::string& url, int timeout = 0)
        {
            return syncGet(makeRequest(url, headers(), "", timeout));
        }

        Reply syncGet(const std::string& url, const headers& head, int timeout = 0)
        {
            return syncGet(makeRequest(url, head, "", timeout));
        }

        Reply syncPost(const Request& req)
        {
            return syncSend(bh::verb::post, req);
        }

        Reply syncPost(const std::string& url, const headers& head, const std::string& body, int timeout = 0)
        {
            return syncPost(makeRequest(url, head, body, timeout));
        }

        Reply syncPut(const Request& req)
        {
            return syncSend(bh::verb::put, req);
        }

        Reply syncPut(const std::string& url, const headers& head, const std::string& body, int timeout = 0)
        {
            return syncPut(makeRequest(url, head, body, timeout));
        }

        Reply syncDel(const Request& req)
        {
            return syncSend(bh::verb::delete_, req);
        }

        Reply syncDel(const std::string& url, const headers& head, const std::string& body, int timeout = 0)
        {
            return syncDel(makeRequest(url, head, body, timeout));
        }

        Reply syncSend(bh::verb verb, const Request& req)
        {
            std::promise<Reply> rep;
            std::future<Reply> future = rep.get_future();
            send(verb, req, [&rep](const Reply& reply)
            {
                rep.set_value(reply);
            });
            return future.get();
        }

        void drop()
        {
            guard_.reset();
            if (!io_.stopped())
            {
                io_.stop();
            }
            for (auto& w : workers_)
            {
                w.join();
            }
        }

    private:
        void doSend(bh::verb verb, const Request& req, const ReplyHandler& func, const net::yield_context& yield)
        {
            if (func == nullptr)
            {
                return;
            }
            if (!req.url.isValid())
            {
                LOG_ERROR("Invalid url, url={}", req.url.toString());
                func(Reply{{ bh::status::not_found, VERSION, "invalid url" }});
                return;
            }

            tcp::resolver resolver(io_);
            beast::error_code ec;
            auto const results = resolver.async_resolve(req.url.host, req.url.port, yield[ec]);
            if (ec)
            {
                LOG_ERROR("Resolve host failed, url={}", req.url.toString());
                func(Reply{{ bh::status::not_found, VERSION, "resolve host failed" }});
                return;
            }

            beast::tcp_stream stream(io_);
            int timeout = req.timeout <= 0 ? timeout_ : req.timeout;
            if (timeout > 0)
            {
                stream.expires_after(std::chrono::seconds(timeout));
            }
            stream.async_connect(results, yield[ec]);
            if (ec)
            {
                LOG_ERROR("Connect to host failed, url={}", req.url.toString());
                func(Reply{{ bh::status::network_connect_timeout_error, VERSION, "connect to host failed" }});
                return;
            }

            bh::request<bh::string_body> br{ verb, req.url.resource, VERSION };
            br.set(bh::field::host, req.url.host);
            br.set(bh::field::user_agent, BOOST_BEAST_VERSION_STRING);
            br.set(bh::field::content_length, req.body.size());
            for (const auto& it : req.head)
            {
                br.set(it.first, it.second);
            }
            br.body() = req.body;

            bh::async_write(stream, br, yield[ec]);
            if (ec)
            {
                LOG_ERROR("Send request failed, url={}, error={}", req.url.toString(), ec.message());
                func(Reply{{ bh::status::internal_server_error, VERSION, "send request failed" }});
                return;
            }

            beast::flat_buffer b;
            bh::response<bh::string_body> rep;
            bh::async_read(stream, b, rep, yield[ec]);
            if (ec)
            {
                LOG_ERROR("Receive response failed, url={}, error={}", req.url.toString(), ec.message());
                func(Reply{{ bh::status::internal_server_error, VERSION, ec.message() }});
                return;
            }

            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            if (ec && ec != beast::errc::not_connected)
            {
                LOG_ERROR("Close connection failed, url={}, error={}", req.url.toString(), ec.message());
                func(Reply{{ bh::status::internal_server_error, VERSION, ec.message() }});
                return;
            }
            func(Reply{ rep });
        }

    private:
        net::io_context io_{ 1 };
        net::executor_work_guard<net::io_context::executor_type> guard_;
        std::vector<std::thread> workers_;
        int timeout_{ 30 };
    };
};

using HttpHeader = http::headers;
using HttpUrl = http::Url;
using HttpRequest = http::Request;
using HttpReply = http::Reply;
using HttpClient = http::Client;
