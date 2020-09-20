#pragma once

#include "MockTask.h"
#include <string>
#include <vector>
#include <functional>

using Message = std::vector<uint8_t>;
using MessageHandler = std::function<void(const Message&)>;


class MockItf
{
public:
    virtual ~MockItf() = default;

public:
    virtual bool init(MockTaskPtr task, MessageHandler inHandler, MessageHandler outHandler) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

