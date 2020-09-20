#pragma once

#include <Logger/Logger.h>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>


namespace ws
{
    namespace beast = boost::beast;         // from <boost/beast.hpp>
    namespace http = beast::http;           // from <boost/beast/http.hpp>
    namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
    namespace net = boost::asio;            // from <boost/asio.hpp>
    using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

    using MessageHandler = std::function<void(const std::string&)>;

    static const std::string WEBSOCKET_CLIENT_VERSION{ "Asula/1.0 WebSocket Client, based on boost beast" };

    struct Url
    {
        std::string protocol{ "ws" };
        std::string host;
        uint16_t port{ 80 };
        std::string target{ "/" };

        bool isValid() const
        {
            return protocol == "ws" && !host.empty() && port > 0 && !target.empty();
        }
    };

    Url UrlFromString(const std::string& url)
    {
        Url u;
        if (!boost::istarts_with(url, "ws://"))
        {
            return u;
        }
        std::string temp{ url.substr(5) };
        auto colonPos = temp.find_first_of(':');
        bool hasColon = colonPos != std::string::npos;
        auto slashPos = temp.find_first_of('/');
        bool hasSlash = slashPos != std::string::npos;
        if (hasColon)
        {
            u.host = temp.substr(0, colonPos);
            const std::string& portStr = temp.substr(colonPos + 1, hasSlash ? slashPos - colonPos : -1);
            try
            {
                u.port = boost::lexical_cast<uint16_t>(portStr);
            }
            catch (const std::exception&)
            {
                u.port = 0;
                return u;
            }
        }
        else
        {
            u.host = temp.substr(0, hasSlash ? slashPos : -1);
            u.port = 80;
        }
        if (hasSlash)
        {
            u.target = temp.substr(slashPos);
        }
        return u;
    }

    class Client final
    {
    public:
        Client(int threadCount = 1)
            : guard_(net::make_work_guard(io_))
            , strand_(io_)
            , wstream_(io_)
        {
            for (auto i = 0; i < threadCount; ++i)
            {
                workers_.emplace_back([=]
                {
                    io_.run();
                });
            }
        }

        ~Client()
        {
            stopReceiver();

            if (!io_.stopped())
            {
                io_.stop();
            }
            for (auto& w : workers_)
            {
                w.join();
            }
        }

    public:
        bool open(const std::string& url)
        {
            const Url& u = UrlFromString(url);
            if (!u.isValid())
            {
                LOG_ERROR("Invalid argument, url={}", url);
                return false;
            }

            tcp::resolver resolver(io_);
            std::string host = u.host;
            std::string port = std::to_string(u.port);
            boost::system::error_code ec;
            auto const results = resolver.resolve(host, port, ec);
            if (ec)
            {
                LOG_ERROR("Resolve failed, {}", ec.message());
                return false;
            }
            beast::get_lowest_layer(wstream_).expires_after(std::chrono::seconds(5));
            auto ep = beast::get_lowest_layer(wstream_).connect(results, ec);
            if (ec)
            {
                LOG_ERROR("Connection failed, {}", ec.message());
                return false;
            }

            host += ':' + std::to_string(ep.port());
            beast::get_lowest_layer(wstream_).expires_never();

            wstream_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
            wstream_.set_option(websocket::stream_base::decorator([](websocket::request_type& req)
            {
                req.set(http::field::user_agent, WEBSOCKET_CLIENT_VERSION);
            }));

            wstream_.handshake(host, u.target, ec);
            if (ec)
            {
                LOG_ERROR("Handshake failed, {}", ec.message());
                return false;
            }
            return true;
        }

        void onMessage(MessageHandler handler)
        {
            handler_ = handler;

            if (handler_ == nullptr)
            {
                stopReceiver();
            }
            else
            {
                startReceiver();
            }
        }

        void send(const std::string& message)
        {
            net::spawn(strand_, std::bind(&Client::doSend, this, message, std::placeholders::_1));
        }

        void close()
        {
            net::spawn(strand_, std::bind(&Client::doClose, this, std::placeholders::_1));
        }

    private:
        void doSend(const std::string& message, net::yield_context yield)
        {
            if (!wstream_.is_open())
            {
                return;
            }
            boost::system::error_code ec;
            wstream_.async_write(net::buffer(message), yield[ec]);
            if (ec)
            {
                LOG_ERROR("Write failed, {}", ec.message());
                return;
            }
        }

        void doClose(net::yield_context yield)
        {
            if (!wstream_.is_open())
            {
                return;
            }
            boost::system::error_code ec;
            wstream_.async_close(websocket::close_code::normal, yield[ec]);
            if (ec)
            {
                LOG_ERROR("Close failed, {}", ec.message());
                return;
            }
            LOG_INFO("WebSocket close");
        }

        void doReceive(net::yield_context yield)
        {
            while (!interrupted_)
            {
                LOG_TRACE("Start receiving");
                boost::system::error_code ec;
                beast::flat_buffer buffer;
                wstream_.async_read(buffer, yield[ec]);
                if (ec)
                {
                    LOG_ERROR("Read failed, {}", ec.message());
                    return;
                }
                if (handler_ != nullptr)
                {
                    const auto& d = buffer.data();
                    std::string msg((const char*)d.data(), d.size());
                    handler_(msg);
                }
                //std::cout << beast::make_printable(buffer.data()) << std::endl;
            }
        }

        void startReceiver()
        {
            if (receiveThread_ != nullptr && !interrupted_)
            {
                LOG_WARN("Receiver is already running");
                return;
            }
            if (handler_ == nullptr)
            {
                LOG_ERROR("Message Handler is null, call onMessage first");
                return;
            }
            if (!wstream_.is_open())
            {
                LOG_ERROR("WebSocket stream is not opend, call open first");
                return;
            }
            interrupted_ = false;
            receiveThread_ = std::make_shared<std::thread>([=]
            {
                net::spawn(strand_, std::bind(&Client::doReceive, this, std::placeholders::_1));
            });
        }

        void stopReceiver()
        {
            interrupted_ = true;
            if (receiveThread_ != nullptr && receiveThread_->joinable())
            {
                receiveThread_->join();
                receiveThread_.reset();
            }
            receiveThread_ = nullptr;

            boost::asio::make_strand(io_);
        }

    private:
        net::io_context io_{ 1 };
        net::executor_work_guard<net::io_context::executor_type> guard_;
        net::io_context::strand strand_;
        websocket::stream<beast::tcp_stream> wstream_;
        std::vector<std::thread> workers_;
        std::atomic<bool> interrupted_{ false };
        std::shared_ptr<std::thread> receiveThread_{ nullptr };
        MessageHandler handler_{ nullptr };
    };
};

using WebSocketClient = ws::Client;
