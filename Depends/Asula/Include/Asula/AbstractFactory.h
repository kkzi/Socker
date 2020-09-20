#pragma once

#include <Logger/Logger.h>
#include <boost/functional/factory.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <unordered_map>
#include <functional>

template<typename T>
class AbstractFactory final
{
    using Factor = std::function<std::shared_ptr<T>()>;

public:
    AbstractFactory() = default;
    AbstractFactory(const AbstractFactory&) = delete;
    AbstractFactory(AbstractFactory&&) = delete;
    AbstractFactory& operator=(const AbstractFactory&) = delete;
    AbstractFactory& operator=(AbstractFactory&&) = delete;

    ~AbstractFactory()
    {
        factories_.clear();
    }

public:
    template<typename U>
    void Register(const std::string& name)
    {
        const auto& lower = boost::to_lower_copy(name);
        if (factories_.count(lower) != 0)
        {
            LOG_WARN("Duplicate register factor, will be override, name={}", lower);
        }
        factories_[lower] = boost::factory<std::shared_ptr<U>>();
    }

    std::shared_ptr<T> Create(const std::string& name)
    {
        const auto& lower = boost::to_lower_copy(name);
        if (factories_.count(lower) == 0)
        {
            return nullptr;
        }
        return factories_.at(lower)();
    }

private:
    std::unordered_map<std::string, Factor> factories_;
};

