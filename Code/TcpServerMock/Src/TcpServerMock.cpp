#include "TcpServerMock.h"
#include <cassert>

bool TcpServerMock::init(MockTaskPtr task, MessageHandler inHandler, MessageHandler outHandler)
{
    task_ = task;
    inFunc_ = inHandler;
    outFunc_ = outHandler;
    return true;
}

void TcpServerMock::start()
{
    switch (task_->type)
    {
    case MockTaskItf::File:
        return startServer(std::static_pointer_cast<FileMockTask>(task_));
    case MockTaskItf::Text:
        return startServer(std::static_pointer_cast<TextMockTask>(task_));
    default:
        throw std::logic_error("Not implemented");
    }
}

void TcpServerMock::stop()
{
    interrupted_ = true;
}

void TcpServerMock::startServer(const std::shared_ptr<FileMockTask>& task)
{
    assert(task!= nullptr);
}

void TcpServerMock::startServer(const std::shared_ptr<TextMockTask>& task)
{
    assert(task!= nullptr);

}
