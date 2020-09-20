#pragma once

#include "ServerContext.hpp"
#include "ServiceItf.h"
#include "HttpServer.hpp"
#include "Util/DllLoader.hpp"
#include <Logger/Logger.h>
#include <boost/algorithm/string.hpp>
#include <memory>
#include <list>
#include <string>


class Prometheus final
{
public:
    Prometheus(int threadCount = 1, const std::string& webRoot = "./web")
        : ctx_(new ServerContext)
    {
        s_ = std::make_unique<HttpServer>(webRoot, threadCount);
    }

    ~Prometheus()
    {

    }

public:
    ServerContextPtr context() const
    {
        return ctx_;
    }

    void setPrefix(const std::string& prefix)
    {
        prefix_ = prefix;
    }

    void loadServices(const std::string& path)
    {
        loader_.loadAll(path, [=](const DllLoadResult& dlr)
        {
            if (!dlr.ok)
            {
                LOG_ERROR("Load service dll failed, path={}, error={}", dlr.path, dlr.message);
                return;
            }

            try
            {
                const std::string& funcName = "getServiceInstance";
                auto func = dlr.lib.get<ServiceItf* ()>(funcName);
                if (func == nullptr)
                {
                    LOG_WARN("Invalid service dll, path={}", dlr.path);
                    return;
                }
                registerService(func());
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Load service dll failed, {}", e.what());
            }
        }
        );

        auto count = ctx_->serviceCount();
        if (count == 0)
        {
            LOG_WARN("No available service registerd, path={}", path);
            return;
        }
        LOG_DEBUG("Load {} services from {}", count, path);
    }

    void exec(int port)
    {
        LOG_INFO("Start prometheus at {}", port);
        return s_->listen(port);
    }

private:
    void registerService(ServiceItf* si)
    {
        LOG_INFO("Init service, name={}", si->name());

        std::string err;
        bool ok = si->init(ctx_, err);
        if (!ok)
        {
            LOG_ERROR("Init service failed, err={}", err);
            return;
        }
        ctx_->addService(si);

        for (const auto& r : si->routes())
        {
            //s_->hook(r.method, r.url, r.handler);

            std::string url(r->url);
            if (!boost::starts_with(url, "/"))
            {
                url = "/" + url;
            }
            if (!boost::starts_with(url, "/atom/") && !boost::starts_with(url, "/coredb/") && !boost::starts_with(url, prefix_))
            {
                url = prefix_ + url;
            }
            r->url = url;

            auto func = r->handler;
            assert(func != nullptr);
            if (func == nullptr)
            {
                continue;
            }
            bool ok = s_->hook(r->method, url, [func](const HttpSessionPtr& session) {
                const auto& req = session->request();
                const std::string& method = req.method_string().to_string();
                const std::string& url = session->href();
                const std::string& body = req.body();
                auto len = req.payload_size().value();
                LOG_DEBUG("REQUEST: method={}, url={}, body_size={}, body={}", method, url, len, body);
                func(session);
                LOG_DEBUG("RESPONSE: status code={}, content length={}", session->responseCode(), session->repsponseContentLength());
            });

            if (ok)
            {
                LOG_DEBUG("Register api, method={}, url={}", r->method, url);
            }
            else
            {
                LOG_ERROR("Register api failed, method={}, url={}", r->method, url);
            }
        }
        LOG_INFO("Init service ok, name={}", si->name());
    }

private:
    ServerContextPtr ctx_{ nullptr };
    std::unique_ptr<HttpServer> s_{ nullptr };
    DllLoader loader_;
    std::string prefix_;
};
