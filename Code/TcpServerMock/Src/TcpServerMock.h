#pragma once

#include "MockItf.h"
#include <Asula/TcpClientSync.hpp>
#include <atomic>

class TcpServerMock : public MockItf
{
public:
    ~TcpServerMock() override = default;

public:
    bool init(MockTaskPtr task, MessageHandler inHandler, MessageHandler outHandler) override;
    void start() override;
    void stop() override;

private:
    void startServer(const std::shared_ptr<FileMockTask>&);
    void startServer(const std::shared_ptr<TextMockTask>&);

private:
    MockTaskPtr task_{ nullptr };
    MessageHandler inFunc_{ nullptr };
    MessageHandler outFunc_{ nullptr };

    TcpClientSync tcpc_;
};

