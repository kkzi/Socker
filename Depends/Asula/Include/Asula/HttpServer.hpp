/**
 * http server based on boost::beast
 * release date: 2020/05/08
 */

#pragma once

#include <Logger/Logger.h>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <functional>
#include <memory>
#include <string>
#include <set>
#include <utility>
#include <cstdlib>
#include <cassert>


namespace http
{
    namespace beast = boost::beast;
    namespace bh = beast::http;
    namespace websocket = beast::websocket;
    namespace net = boost::asio;
    using tcp = boost::asio::ip::tcp;
    using string_view = boost::beast::string_view;
    using ws_stream = websocket::stream<beast::tcp_stream>;
    using ws_stream_ptr = std::shared_ptr<ws_stream>;

    class Session;

    using SessionPtr = std::shared_ptr<Session>;
    using HookFunc = std::function<void(const SessionPtr&)>;

    static const char* HTTP_SERVER_VERSION = "Asula/1.0 HTTP Server, based on boost beast\0";
    static const char* WEBSOCKET_SERVER_VERSION = "Asula/1.0 WebSocket Server, based on boost beast\0";

    enum ResponseContextType : uint8_t
    {
        DEFAULT,
        JSON,
        XML,
        TEXT
    };

    static string_view minetype(string_view path)
    {
        static std::map<string_view, string_view> types{
            { "", "text/plain" },
            { ".htm", "text/html" },
            { ".html", "text/html" },
            { ".php", "text/html" },
            { ".css", "text/css" },
            { ".txt", "text/plain" },
            { ".js", "application/javascript" },
            { ".json", "application/json" },
            { ".xml", "application/xml" },
            { ".swf", "application/x-shockwave-flash" },
            { ".flv", "video/x-flv" },
            { ".png", "image/png" },
            { ".jpe", "image/jpeg" },
            { ".jpeg", "image/jpeg" },
            { ".jpg", "image/jpeg" },
            { ".gif", "image/gif" },
            { ".bmp", "image/bmp" },
            { ".ico", "image/vnd.microsoft.icon" },
            { ".tiff", "image/tiff" },
            { ".tif", "image/tiff" },
            { ".svg", "image/svg+xml" },
            { ".svgz", "image/svg+xml" },
        };

        const auto pos = path.rfind(".");
        const auto ext = pos == string_view::npos ? "" : path.substr(pos);
        return types.count(ext) ? types.at(ext) : "";
    }

    static std::string catPath(string_view base, string_view path)
    {
        if (base.empty())
        {
            return path.to_string();
        }
        std::string result = base.to_string();
        char constexpr path_separator = '/';
        if (result.back() == path_separator)
        {
            result.resize(result.size() - 1);
        }
        result.append(path.data(), path.size());
        return result;
    }

    static std::string decodeUri(const string_view& url)
    {
        std::string ret;
        for (size_t i = 0, len = url.length(); i < len; i++)
        {
            if (url[i] == '%')
            {
                uint8_t ch = 0;
                try
                {
                    boost::lexical_cast<uint8_t>(url.substr(i + 1, 2));
                }
                catch (const boost::bad_lexical_cast&)
                {
                    // use default value
                }
                ret += ch;
                i = i + 2;
            }
            else if (url[i] == '+')
            {
                ret += ' ';
            }
            else
            {
                ret += url[i];
            }
        }
        return boost::to_lower_copy(ret);
    }



    /************************************************************************/
    /*                                                                      */
    /************************************************************************/
    struct Route
    {
        std::string method;
        std::string url;
        HookFunc handler;

        Route(const std::string& method, const std::string& url, HookFunc handler)
        {
            this->method = boost::to_upper_copy(method);
            this->url = boost::starts_with(url, "/") ? url : ("/" + url);
            this->handler = std::move(handler);
        }

        ~Route()
        = default;

        [[nodiscard]] bool isValid() const
        {
            return !method.empty() && !url.empty() && handler != nullptr;
        }
    };

    using RoutePtr = std::shared_ptr<Route>;


    /************************************************************************/
    /*                                                                      */
    /************************************************************************/
    class HandlerRegistrar
    {
        friend class Session;

        friend class Server;

    private:
        struct HookFunctor
        {
            std::string url;
            HookFunc func{ nullptr };
            std::vector<std::string> tokens;

            HookFunctor() = default;

            HookFunctor(const std::string& url_, HookFunc func_)
                : url(boost::to_lower_copy(url_))
                , func(std::move(func_))
            {
                tokens = splitUrl(url);
            }

            bool operator==(const HookFunctor& other) const
            {
                if (this == &other || url == other.url)
                {
                    return true;
                }
                if (tokens.size() != other.tokens.size())
                {
                    return false;
                }
                for (auto i = 0; i < tokens.size(); ++i)
                {
                    auto lt = tokens.at(i);
                    auto rt = other.tokens.at(i);
                    if (lt == rt || isFuzzy(lt) && isFuzzy(rt))
                    {
                        continue;
                    }
                    return false;
                }
                return true;
            }

            bool operator<(const HookFunctor& other) const
            {
                if (*this == other)
                {
                    return false;
                }
                return tokens < other.tokens;
            }
        };

        static bool isFuzzy(const std::string& t)
        {
            return boost::starts_with(t, "<") && boost::ends_with(t, ">")
                || boost::starts_with(t, "[") && boost::ends_with(t, "]");
        }

        static std::vector<std::string> splitUrl(const std::string& url)
        {
            std::vector<std::string> tokens;
            const std::string& schema = url.substr(0, url.find_first_of('?', 0));
            boost::split(tokens, boost::to_lower_copy(schema), boost::is_any_of("/"), boost::token_compress_on);
            for (auto it = tokens.begin(); it != tokens.end();)
            {
                if (it->empty())
                {
                    it = tokens.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            return tokens;
        }

        struct MatchedResult
        {
            bool ok{ false };
            HookFunctor func;
            std::map<std::string, std::string> args;
        };

        static MatchedResult match(const std::string& schema, const std::set<HookFunctor>& funcs)
        {
            bool fuzzy = boost::contains(schema, "<") || boost::contains(schema, ">")
                || boost::contains(schema, "[") || boost::contains(schema, "]");
            if (fuzzy)
            {
                return MatchedResult();
            }

            const std::vector<std::string>& tokens = splitUrl(schema);
            int computedWeight = 0;
            MatchedResult computed{ false };
            for (auto f : funcs)
            {
                if (tokens.size() < f.tokens.size())
                {
                    continue;
                }

                std::map<std::string, std::string> args;
                int weight = 0;
                if (f.tokens.empty())
                {
                    weight += 1;
                }
                else
                {
                    for (auto i = 0; i < f.tokens.size(); ++i)
                    {
                        auto ft = f.tokens.at(i);
                        auto st = tokens.at(i);
                        if (ft == st)
                        {
                            weight += 100;
                        }
                        else if (isFuzzy(ft))
                        {
                            auto name = ft.substr(1, ft.size() - 2);
                            args[name] = st;
                            weight += 10;
                        }
                        else
                        {
                            break;
                        }
                    }
                }
                if (weight > computedWeight)
                {
                    computedWeight = weight;
                    computed = MatchedResult{ true, f, args };
                }
            }
            return computed;
        }

    public:
        ~HandlerRegistrar()
        {
            hooks_.clear();
        }

    public:
        inline bool add(boost::beast::http::verb verb, const std::string& target, const HookFunc& func);

    private:
        bool process(const SessionPtr& session);

    private:
        std::mutex mutex_;
        std::map<boost::beast::http::verb, std::set<HookFunctor>> hooks_;
    };

    using HandlerRegistrarPtr = std::shared_ptr<HandlerRegistrar>;



    /************************************************************************/
    /* SendLambda                                                           */
    /************************************************************************/
    struct SendLambda
    {
        beast::tcp_stream& stream_;
        bool& close_;
        beast::error_code& ec_;
        net::yield_context yield_;

        SendLambda(beast::tcp_stream& stream, bool& close, beast::error_code& ec, net::yield_context yield)
            : stream_(stream)
            , close_(close)
            , ec_(ec)
            , yield_(std::move(yield))
        {

        }

        template<bool isRequest, class Body, class Fields>
        void operator()(bh::message<isRequest, Body, Fields>&& msg) const
        {
            // Determine if we should close the connection after
            close_ = msg.need_eof();

            // We need the serializer here because the serializer requires
            // a non-const file_body, and the message oriented version of
            // http::write only works with const messages.
            bh::serializer<isRequest, Body, Fields> sr{ msg };
            bh::async_write(stream_, sr, yield_[ec_]);
        }
    };

    /************************************************************************/
    /* WebSocketGroupHandler                                                */
    /************************************************************************/
    class WebSocketGroupHandler
    {
    public:
        WebSocketGroupHandler()
        = default;

        ~WebSocketGroupHandler()
        {
            for (const auto& it : clients_)
            {
                for (const auto& c : it.second)
                {
                    boost::system::error_code ec;
                    c->close(websocket::close_reason("Server shutdown"), ec);
                }
            }
        }

    public:
        void handle(const bh::request<bh::string_body>& req, beast::tcp_stream& stream, beast::error_code& ec, const net::yield_context& yield)
        {
            auto wstream = std::make_shared<ws_stream>(std::move(stream));
            wstream->set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
            wstream->set_option(websocket::stream_base::decorator([](websocket::response_type& res)
            {
                res.set(bh::field::server, WEBSOCKET_SERVER_VERSION);
            }));

            wstream->async_accept(req, yield[ec]);
            if (ec)
            {
                LOG_ERROR("Accept failed, {}", ec.message());
                return;
            }

            std::string group{ req.target().to_string() };
            boost::to_lower(group);
            join(wstream, group);

            for (;;)
            {
                if (!wstream->is_open())
                {
                    return;
                }
                beast::flat_buffer buffer;
                wstream->async_read(buffer, yield[ec]);
                if (ec == websocket::error::closed)
                {
                    LOG_WARN("{}", ec.message());
                    return;
                }

                multicastMessage(wstream, group, buffer, ec, yield);
            }
        }

    private:
        void join(const ws_stream_ptr& stream, const std::string& group)
        {
            if (group.empty())
            {
                return;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            clients_[group].insert(stream);
        }

        void exit(const ws_stream_ptr& stream, const std::string& group)
        {
            if (group.empty())
            {
                return;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            clients_[group].erase(stream);
        }

        void multicastMessage(const ws_stream_ptr& stream, const std::string& group, beast::flat_buffer& message, beast::error_code& ec, const net::yield_context& yield)
        {
            if (clients_.count(group) == 0)
            {
                LOG_WARN("Group not exist, group={}", group);
                return;
            }
            std::set<ws_stream_ptr> clients;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                clients = clients_.at(group);
            }

            std::list<ws_stream_ptr> closed;
            for (const auto& c : clients)
            {
                if (c == stream)
                {
                    continue;
                }
                if (!c->is_open())
                {
                    closed.push_back(c);
                    continue;
                }
                c->text(stream->got_text());
                c->async_write(message.data(), yield[ec]);
                if (ec)
                {
                    LOG_ERROR("Write failed, {}", ec.message());
                    break;
                }
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& c : closed)
                {
                    clients_[group].erase(c);
                }
            }
        }

    private:
        std::mutex mutex_;
        std::unordered_map<std::string, std::set<ws_stream_ptr>> clients_;
    };


    /************************************************************************/
    /*                                                                      */
    /************************************************************************/
    class Session : public std::enable_shared_from_this<Session>, private boost::noncopyable
    {
        friend class Server;

        friend class HandlerRegistrar;

    public:
        Session(std::string root, bh::request<bh::string_body>&& req, SendLambda&& send)
            : root_(std::move(root))
            , req_(req)
            , send_(send)
        {
            href_ = decodeUri(req_.target());
        }

    public:
        const bh::request<bh::string_body>& request() const
        {
            return req_;
        }

        std::string method() const
        {
            return req_.method_string().to_string();
        }

        std::string href() const
        {
            return href_;
        }

        std::string requestBody() const
        {
            return req_.body();
        }

        std::string arg(const std::string& key, const std::string& value = "") const
        {
            auto k = boost::to_lower_copy(key);
            return args_.count(k) > 0 ? args_.at(k) : value;
        }

        std::map<std::string, std::string> args() const
        {
            return args_;
        }

        int responseCode() const
        {
            return repStatusCode_;
        }

        int responseContentLength() const
        {
            return repContentLen_;
        }

        template<typename Body>
        void reply(bh::response<Body>& rep)
        {
            BOOST_ASSERT_MSG(!replied_, "Duplicate reply");
            if (replied_)
            {
                return;
            }
            replied_ = true;
            repStatusCode_ = (int)rep.result();
            repContentLen_ = (int)rep.payload_size().get();
            return send_(std::move(rep));
        }

        template<ResponseContextType type = DEFAULT>
        void replyText(const std::string& text, bh::status status = bh::status::ok)
        {
            static std::map<int, std::string> contextTypes
                {
                    { DEFAULT, "text/plain" },
                    { JSON, "application/json" },
                    { XML, "application/xml" },
                    { TEXT, "text/plain" },
                };
            bh::response<bh::string_body> res{ status, req_.version() };
            res.set(bh::field::content_type, contextTypes.at(type) + "; charset=utf-8");
            res.keep_alive(req_.keep_alive());
            res.body() = text;
            res.prepare_payload();
            return reply(res);
        };

        void replyLocalFile(const std::string& path, std::string name = "")
        {
            boost::beast::error_code ec;
            bh::file_body::value_type body;
            body.open(path.c_str(), boost::beast::file_mode::scan, ec);

            if (ec == boost::system::errc::no_such_file_or_directory)
            {
                return replyNotFound();
            }

            if (ec)
            {
                return replyServerError(ec.message());
            }

            if (req_.method() == bh::verb::head)
            {
                bh::response<bh::empty_body> res{ bh::status::ok, req_.version() };
                res.set(bh::field::server, HTTP_SERVER_VERSION);
                res.set(bh::field::content_type, minetype(path));
                res.content_length(body.size());
                res.keep_alive(req_.keep_alive());
                return reply(res);
            }

            auto len = body.size();
            bh::response<bh::file_body> res
                {
                    std::piecewise_construct,
                    std::make_tuple(std::move(body)), std::make_tuple(bh::status::ok, req_.version())
                };
            res.set(bh::field::server, HTTP_SERVER_VERSION);
            auto mtype = minetype(path);
            if (mtype.empty())
            {
                res.set(bh::field::content_type, mtype);
                if (path.rfind('/') != std::string::npos)
                {
                    if (name.empty())
                    {
                        name = path.substr(path.rfind('/') + 1);
                    }
                    res.set(bh::field::content_disposition, "attachment;filename=" + name);
                }
            }
            else
            {
                res.set(bh::field::content_type, mtype);
            }
            res.content_length(len);
            res.keep_alive(req_.keep_alive());
            return reply(res);
        }

        void replyLocalFile()
        {
            const auto& url = href();
            std::string path = catPath(root_, url);
            if (url.back() == '/')
            {
                path.append("index.html");
            }
            return replyLocalFile(path);
        }

        void replyBadRequest(const std::string& why)
        {
            return replyText(why, bh::status::bad_request);
        };

        void replyNotFound(const std::string& what = "")
        {
            auto res = what.empty() ? req_.target().to_string() : what;
            return replyText("The resource '" + res + "' was not found.", bh::status::not_found);
        };

        void replyUnauthorized()
        {
            return replyText("Unauthorized request.", bh::status::unauthorized);
        }

        void replyServerError(const std::string& what)
        {
            return replyText("An error occurred: '" + what + "'.", bh::status::internal_server_error);
        };

    private:
        void handleRequest(const HandlerRegistrarPtr& registrar)
        {
            const auto& method = req_.method();
            const auto& url = req_.target();

            if (method == bh::verb::unknown)
            {
                return replyBadRequest("Unknown HTTP-method");
            }

            if (url.empty() || url[0] != '/' || url.find("..") != string_view::npos)
            {
                return replyBadRequest("Illegal request-target");
            }

            parseArguments();
            try
            {
                bool ok = registrar->process(shared_from_this());
                if (ok)
                {
                    return;
                }
            }
            catch (const std::exception& e)
            {
                return replyText(e.what(), bh::status::internal_server_error);
            }

            return replyLocalFile();
        }

        void parseArguments()
        {
            args_.clear();
            std::string url = href();
            if (url.find('?') == std::string::npos)
            {
                return;
            }
            url = url.substr(url.find_first_of('?') + 1);
            std::string key;
            std::string value;
            bool inKeyTurn = true;
            for (char c : url)
            {
                switch (c)
                {
                case '&':
                    if (!key.empty() || !value.empty())
                    {
                        args_[boost::to_lower_copy(key)] = value;
                    }
                    key.clear();
                    value.clear();
                    inKeyTurn = true;
                    break;
                case '=':
                    inKeyTurn = false;
                    break;
                case '#':
                    break;
                default:
                    inKeyTurn ? key += c : value += c;
                    break;
                }
            }
            if (!key.empty() || !value.empty())
            {
                args_[boost::to_lower_copy(key)] = value;
            }
        }

        void addArgument(const std::string& key, const std::string& val)
        {
            args_[key] = val;
        }

    private:
        const std::string root_;
        bh::request<bh::string_body> req_;
        SendLambda send_;

        std::shared_ptr<void> res_;
        std::string href_;
        std::map<std::string, std::string> args_;

        int repStatusCode_{ (int)bh::status::ok };
        int repContentLen_{ 0 };
        bool replied_{ false };
    };


    /************************************************************************/
    /*                                                                      */
    /************************************************************************/
    class Server
    {
    public:
        explicit Server(std::string wwwRoot = "./www", int threadCount = 1)
            : port_(8014)
            , threadCount_(threadCount)
            , timeout_(0)
            , io_(threadCount)
            , docRoot_(std::move(wwwRoot))
            , registrar_(std::make_shared<HandlerRegistrar>())
        {
            enableListApi();
        }

        ~Server()
        {
            stop();
        }

    public:
        // Call this method before run
        void setTimeout(int seconds)
        {
            timeout_ = seconds;
        }

        // Register a hook function to handle custom requests
        template<boost::beast::http::verb verb>
        bool hook(const std::string& target, const HookFunc& func)
        {
            return hook(verb, target, func);
        }

        template<char* method>
        bool hook(const std::string& target, const HookFunc& func)
        {
            return hook(std::string(method), target, func);
        }

        bool hook(bh::verb verb, const std::string& target, const HookFunc& func)
        {
            assert(verb != bh::verb::unknown);
            assert(!target.empty());
            assert(func != nullptr);
            return registrar_->add(verb, target, func);
        }

        bool hook(const std::string& method, const std::string& target, const HookFunc& func)
        {
            auto verb = bh::string_to_verb(boost::to_upper_copy(method));
            return hook(verb, target, func);
        }

        bool hook(const Route& r)
        {
            return hook(r.method, r.url, r.handler);
        }

        void listen(int port = 0)
        {
            if (port > 0)
            {
                port_ = port;
            }
            net::spawn(io_, std::bind(&Server::doListen, this, std::placeholders::_1));

            std::vector<std::thread> threads;
            threads.reserve(threadCount_);
            for (auto i = 0; i < threadCount_; ++i)
            {
                threads.emplace_back([=]
                {
                    boost::system::error_code ec;
                    io_.run(ec);
                });
            }
            for (auto& t : threads)
            {
                t.join();
            }
        }

        void stop()
        {
            registrar_.reset();
            if (!io_.stopped())
            {
                io_.stop();
            }
        }

    private:
        void enableListApi()
        {
            static const std::string API_LIST = "/$apis";

            hook("GET", API_LIST, [&](const SessionPtr& session)
            {
                std::stringstream ss;
                for (const auto& it : registrar_->hooks_)
                {
                    for (const auto& h : it.second)
                    {
                        if (h.url != API_LIST)
                        {
                            ss << std::setw(6) << it.first << " " << h.url << "\n";
                        }
                    }
                }
                std::string txt{ ss.str() };
                if (txt.empty())
                {
                    txt = "No apis";
                }
                session->replyText(txt);
                return true;
            });
        }

        void doListen(const net::yield_context& yield)
        {
            beast::error_code ec;

            tcp::endpoint endpoint{ tcp::v4(), port_ };
            tcp::acceptor acceptor(io_);
            acceptor.open(endpoint.protocol(), ec);
            if (ec)
            {
                LOG_ERROR("Open host failed, {}", ec.message());
                return;
            }

            acceptor.set_option(net::socket_base::reuse_address(true), ec);
            if (ec)
            {
                LOG_ERROR("Set option failed, {}", ec.message());
                return;
            }

            acceptor.bind(endpoint, ec);
            if (ec)
            {
                LOG_ERROR("Bind failed, {}", ec.message());
                return;
            }

            acceptor.listen(net::socket_base::max_listen_connections, ec);
            if (ec)
            {
                LOG_ERROR("Listen failed, {}", ec.message());
                return;
            }

            for (;;)
            {
                tcp::socket socket(io_);
                acceptor.async_accept(socket, yield[ec]);
                if (ec)
                {
                    LOG_ERROR("Accept failed, {}", ec.message());
                    continue;
                }
                net::spawn(acceptor.get_executor(), std::bind(&Server::doConnection, this, std::move(socket), std::placeholders::_1));
            }
        }

        // Handles an HTTP server connection
        void doConnection(tcp::socket& socket, const net::yield_context& yield)
        {
            beast::error_code ec;
            beast::flat_buffer buffer;
            bool close = false;

            beast::tcp_stream stream(std::move(socket));
            for (;;)
            {
                if (timeout_ > 0)
                {
                    stream.expires_after(std::chrono::seconds(timeout_));
                }

                bh::request<bh::string_body> req;
                bh::async_read(stream, buffer, req, yield[ec]);
//                if (ec == bh::error::end_of_stream)
//                {
//                    break;
//                }
//                else
                if (ec)
                {
//                    LOG_WARN("Read failed, code={}, err={}", ec.value(), ec.message());
                    break;
                }

                if (websocket::is_upgrade(req))
                {
                    wshandler_.handle(req, stream, ec, yield);
                    break;
                }
                else
                {
                    SendLambda lambda{ stream, close, ec, yield };
                    doProcessRequest(req, lambda);
                }

                if (close)
                {
                    break;
                }
            }
            stream.socket().shutdown(tcp::socket::shutdown_send, ec);
        }

        void doProcessRequest(bh::request<bh::string_body>& req, SendLambda& lambda)
        {
            auto s = std::make_shared<Session>(docRoot_, std::move(req), std::move(lambda));
            LOG_DEBUG("HTTP REQ: {} {}", s->method(), s->href());
            s->handleRequest(registrar_);
            if (!s->replied_)
            {
                BOOST_ASSERT_MSG(!s->replied_, "No reply");
                s->replyServerError("No reply");
            }
            LOG_DEBUG("HTTP REP: {} {} {}", s->method(), s->href(), s->responseCode());
        }


    private:
        net::io_context io_;
        uint16_t port_{ 0 };
        int threadCount_{ 1 };
        int timeout_{ 0 };
        std::string docRoot_;
        HandlerRegistrarPtr registrar_;
        WebSocketGroupHandler wshandler_;
    };

    inline bool HandlerRegistrar::add(bh::verb verb, const std::string& target, const HookFunc& func)
    {
        if (verb == bh::verb::unknown || target.empty() || func == nullptr)
        {
            LOG_ERROR("Invalid argument, method={}, target={}", bh::to_string(verb).to_string(), target);
            return false;
        }
        std::string url = boost::to_lower_copy(target);
        auto[_, ok] = hooks_[verb].emplace(HookFunctor{ url, func });
        if (!ok)
        {
            LOG_ERROR("Duplicate registration, method={}, target={}", bh::to_string(verb).to_string(), target);
        }
        return ok;
    }

    inline bool HandlerRegistrar::process(const SessionPtr& session)
    {
        const auto& met = session->request().method();
        //std::lock_guard<std::mutex> lock(mutex_);
        if (hooks_.count(met) == 0)
        {
            return false;
        }

        const auto& funcs = hooks_.at(met);
        const std::string& href = session->href();
        const std::string& schema = href.substr(0, href.find_first_of('?', 0));
        auto ret = match(schema, funcs);
        if (!ret.ok)
        {
            return false;
        }
        for (const auto& a : ret.args)
        {
            session->addArgument(a.first, a.second);
        }
        ret.func.func(session);
        return true;
    }

}  // namespace http

using Route = http::Route;
using RoutePtr = http::RoutePtr;
using HttpServer = http::Server;
using HttpHandler = http::HookFunc;
using HttpSessionPtr = http::SessionPtr;

