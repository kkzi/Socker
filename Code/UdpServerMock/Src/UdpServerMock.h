#pragma once

#include "MockItf.h"

class UdpServerMock : MockItf
{
public:
    ~UdpServerMock() override = default;

public:
    bool init(const std::string& string) override;
    void start() override;
    void stop() override;
};

