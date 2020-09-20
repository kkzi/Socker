#pragma once

#include <boost/asio/read.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/multicast.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/spawn.hpp>
#include <Util/IpUtil.hpp>
#include <Util/TimeUtil.hpp>
#include <Logger/Logger.h>
#include <functional>
#include <thread>
#include <utility>
#include <vector>
#include <array>
#include <atomic>
#include <cstdint>
#include <cassert>

static const int BUFFER_MAX_LEN = 0xFFFF;

class MulticastReceiver
{
public:
    using Message = std::vector<uint8_t>;
    using MessageHandler = std::function<void(double time, const Message& msg)>;

public:
    MulticastReceiver()
        : MulticastReceiver("", "", 0, nullptr)
    {

    }

    MulticastReceiver(std::string localIp, std::string destIp, uint16_t port, MessageHandler handler)
        : guard_(boost::asio::make_work_guard(io_))
        , socket_(io_)
        , localIp_(std::move(localIp))
        , destIp_(std::move(destIp))
        , port_(port)
        , handler_(std::move(handler))
    {

    }

    ~MulticastReceiver()
    {
        stop();
    }

public:
    MulticastReceiver& setLocalIp(const std::string& ip)
    {
        localIp_ = ip;
        return *this;
    }

    MulticastReceiver& setDestIp(const std::string& ip)
    {
        destIp_ = ip;
        return *this;
    }

    MulticastReceiver& setPort(int port)
    {
        port_ = port;
        return *this;
    }

    MulticastReceiver& setMessageHandler(MessageHandler handler)
    {
        handler_ = std::move(handler);
        return *this;
    }

    bool start()
    {
        err_.clear();

        assert(handler_ != nullptr);
        if (handler_ == nullptr)
        {
            err_ = "Invalid handler";
            LOG_ERROR(err_);
            return false;
        }

        if (destIp_.empty() || port_ <= 0)
        {
            err_ = fmt::format("Invalid multicast ip or port settings. dest ip={}, port={}", destIp_, port_);
            LOG_ERROR(err_);
            return false;
        }
        if (handler_ == nullptr)
        {
            err_ = "Invalid message handler";
            LOG_ERROR(err_);
            return false;
        }
        if (socket_.is_open())
        {
            err_ = "Duplicate socket open";
            LOG_ERROR(err_);
            return false;
        }

        try
        {
            if (localIp_.empty())
            {
                localIp_ = IpUtil::getSpeculativeIpV4();
            }
            boost::asio::ip::address addr = localIp_.empty() ? boost::asio::ip::address_v4() : boost::asio::ip::make_address(localIp_);
            LOG_INFO("Start multicast receiver, local ip={}, dest ip={}, port={}", addr.to_v4().to_string(), destIp_, port_);

            boost::asio::ip::udp::endpoint listenEndpoint(addr, port_);
            socket_.open(listenEndpoint.protocol());
            socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
            socket_.bind(listenEndpoint);
            socket_.set_option(boost::asio::ip::multicast::join_group(boost::asio::ip::make_address(destIp_)));

            worker_ = std::thread([&]
                {
                    try
                    {
                        io_.run();
                    }
                    catch (const std::exception& e)
                    {
                        err_ = fmt::format("Multicast receiver exception: {}", e.what());
                        LOG_ERROR(err_);
                    }
                }
            );

            LOG_INFO("Multicast receiver started");
            doReceive();
            return true;
        }
        catch (const boost::system::system_error& e)
        {
            err_ = fmt::format("Join multicast group exception, local ip={}, group ip={}, port={}, exception={}", localIp_, destIp_, port_, e.what());
            LOG_ERROR(err_);
            return false;
        }
    }

    void stop()
    {
        err_.clear();
        if (!isRunning())
        {
            return;
        }
        LOG_INFO("Stop multicast receiver");
        guard_.reset();
        io_.stop();
        if (worker_.joinable())
        {
            worker_.join();
        }
        LOG_INFO("Multicast receiver stopped");
    }

    [[nodiscard]] bool isRunning() const
    {
        return !io_.stopped();
    }

    [[nodiscard]] std::string error() const
    {
        return err_;
    }

private:
    void doReceive()
    {
        msg_.resize(BUFFER_MAX_LEN);
        socket_.async_receive_from(boost::asio::buffer(msg_), sender_, [this](boost::system::error_code ec, std::size_t length)
        {
            if (ec)
            {
                LOG_ERROR("Receiving failed, {}", ec.message());
            }
            else if (handler_ != nullptr)
            {
                msg_.resize(length);
                handler_(TimeUtil::getCurrentEpochS(), msg_);
                doReceive();

                msgCount_ += 1;
                if (msgCount_ % 10000 == 1)
                {
                    LOG_DEBUG("Received total {} message", msgCount_);
                }
            }
        });
    }

private:
    boost::asio::io_context io_{ 1 };
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint sender_;
    std::thread worker_;
    std::string localIp_;
    std::string destIp_;
    uint16_t port_{ 0 };
    MessageHandler handler_;
    Message msg_;
    std::atomic<int> msgCount_{ 0 };
    std::string err_;
};