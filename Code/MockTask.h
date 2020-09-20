#pragma once

#include <string>
#include <memory>

struct MockTaskItf
{
    enum Type
    {
        File,
        Text
    };

    virtual ~MockTaskItf() = default;

    Type type;
    std::string ip{ "127.0.0.1" };
    int16_t port{ 0 };
    int16_t loopCount{ 0 };
};

using MockTaskPtr = std::shared_ptr<MockTaskItf>;


struct TextMockTask : public MockTaskItf
{
    std::string text;
};

struct FileMockTask : public MockTaskItf
{
    std::string filepath;
};
