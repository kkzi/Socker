#pragma once

#include <Asula/HttpServer.hpp>
#include <list>
#include <string>
#include <functional>

class ServerContext;
using ServerContextPtr = std::shared_ptr<ServerContext>;
using RouteList = std::list<RoutePtr>;


class ServiceItf
{
public:
    virtual ~ServiceItf() = 0 {};

public:
    virtual std::string name() const = 0;
    virtual RouteList routes() = 0;

    virtual bool init(ServerContextPtr config, std::string& err) = 0;

    //virtual void activate() = 0;
    //virtual void deactivate() = 0;
};


#ifndef ROUTE
#define ROUTE(METHOD, URL, FUNC, POINTER) std::make_shared<Route>(METHOD, URL, std::bind(&FUNC, POINTER, std::placeholders::_1))
#endif // !ROUTE


#include <boost/config.hpp>

#define DECLEAR_SERVICE(TYPE) \
extern "C" BOOST_SYMBOL_EXPORT ServiceItf * getServiceInstance() \
{\
    static TYPE inst; \
    return &inst; \
}
