#pragma once

#include "ServiceItf.h"
#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>
#include <cassert>

class ServiceItf;

class ServerContext final
{
    friend class Prometheus;

public:
    size_t serviceCount() const
    {
        return services_.size();
    }

    ServiceItf* getService(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(serviceMutex_);
        return services_.count(name) == 0 ? nullptr : services_.at(name);
    }

    void setConfig(const std::string& name, const std::string& value)
    {
        // overwrite
        std::lock_guard<std::mutex> lock(confMutex_);
        conf_[name] = value;
    }

    std::string getConfig(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(confMutex_);
        return conf_.count(name) == 0 ? std::string() : conf_.at(name);
    }

private:
    void addService(ServiceItf* service)
    {
        assert(service != nullptr);
        std::lock_guard<std::mutex> lock(serviceMutex_);
        services_[service->name()] = service;
    }

private:
    std::mutex serviceMutex_;
    std::unordered_map<std::string, ServiceItf*> services_;

    std::mutex confMutex_;
    std::unordered_map<std::string, std::string> conf_;
};


using ServerContextPtr = std::shared_ptr<ServerContext>;
