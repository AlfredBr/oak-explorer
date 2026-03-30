// src/oak/Device.h
#pragma once
#include <string>
#include <chrono>

namespace oak {

struct DeviceInfo {
    std::string mxId;
    std::string name;
    bool available;  // false if already claimed by another process
};

class OakDevice {
public:
    // Polls for connected devices and updates internal state.
    // Call once per frame. Does not throw.
    void poll();

    bool isConnected() const { return connected_; }
    const DeviceInfo& info() const { return info_; }

private:
    bool connected_ = false;
    DeviceInfo info_;
    std::chrono::steady_clock::time_point lastPoll_{};  // throttle USB enumeration
};

} // namespace oak
