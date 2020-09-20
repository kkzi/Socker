#pragma once

#include <Logger/Logger.h>
#include <boost/asio.hpp>
#include <memory>
#include <utility>
#include <vector>
#include <string>
#include <cstdint>

class TcpClientSync final
{
public:
    explicit TcpClientSync(std::shared_ptr<boost::asio::io_context> io)
        : io_(std::move(io))
        , socket_(*io_)
    {

    }

    TcpClientSync()
        : TcpClientSync(std::make_shared<boost::asio::io_context>())
    {

    }

    ~TcpClientSync()
    {
        disconnect();
    }

public:
    bool bind(const std::string& ip, uint16_t port = 0)
    {
        err_.clear();

        using tcp = boost::asio::ip::tcp;
        if (isConnected())
        {
            err_ = "Already connected";
            LOG_ERROR(err_);
            return false;
        }

        if (socket_.is_open())
        {
            boost::system::error_code ignored_ec;
            socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
            socket_.close();
        }
        socket_.open(boost::asio::ip::tcp::v4());
        tcp::endpoint ep(boost::asio::ip::address::from_string(ip), port);
        boost::system::error_code ec;
        socket_.bind(ep, ec);
        if (ec)
        {
            err_ = fmt::format("Bind failed, err={}", ec.message());
            LOG_ERROR(err_);
            return false;
        }
        LOG_INFO("Bind ok, address={}:{}", ip, port);
        return true;
    }

    bool isConnected()
    {
        return socket_.is_open();
    }

    bool connectTo(const std::string& ip, uint16_t port)
    {
        err_.clear();

        if (isConnected())
        {
            LOG_WARN("Duplicate connect, address={}:{}", ip, port);
            return true;
        }

        boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string(ip), port);
        boost::system::error_code ec;
        socket_.connect(ep, ec);
        if (ec)
        {
            err_ = fmt::format("Connect failed, address={}:{}", ip, port);
            LOG_ERROR(err_);
            return false;
        }
        return true;
    }

    void disconnect()
    {
        err_.clear();

        if (socket_.is_open())
        {
            boost::system::error_code ignore;
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignore);
            socket_.close();
        }
    }

    template<typename PodType, typename Allocator>
    size_t send(const std::vector<PodType, Allocator>& buf)
    {
        err_.clear();

        if (!isConnected())
        {
            err_ = "No connection";
            LOG_ERROR(err_);
            return 0;
        }

        boost::system::error_code ec;
        auto len = boost::asio::write(socket_, boost::asio::buffer(buf), ec);
        if (ec)
        {
            err_ = fmt::format("Send failed, err={}", ec.message());
            LOG_ERROR(err_);
            disconnect();
            return 0;
        }
        return len;
    }

    template<typename PodType, typename Allocator>
    size_t receive(std::vector<PodType, Allocator>& buf, size_t size)
    {
        err_.clear();

        if (!isConnected())
        {
            err_ = "No connection";
            LOG_ERROR(err_);
            return 0;
        }
        boost::system::error_code ec;
        size_t len = boost::asio::read(socket_, boost::asio::buffer(buf, size), ec);
        if (ec)
        {
            err_ = fmt::format("Read failed, err={}", ec.message());
            LOG_ERROR(err_);
            disconnect();
            return 0;
        }
        return len;
    }

    template<typename PodType, typename Allocator>
    size_t receive(std::vector<PodType, Allocator>& buf)
    {
        err_.clear();

        if (!isConnected())
        {
            err_ = "No connection";
            LOG_ERROR(err_);
            return 0;
        }
        boost::system::error_code ec;
        size_t len = socket_.read_some(boost::asio::buffer(buf), ec);
        if (ec)
        {
            err_ = fmt::format("Read failed, err={}", ec.message());
            LOG_ERROR(err_);
            disconnect();
            return 0;
        }
        return len;
    }

    [[nodiscard]] std::string error() const
    {
        return err_;
    }

private:
    std::shared_ptr<boost::asio::io_context> io_{ nullptr };
    boost::asio::ip::tcp::socket socket_;
    std::string err_;
};

using TcpClientSyncPtr = std::shared_ptr<TcpClientSync>;

