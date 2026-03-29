// src/oak/Device.cpp
#include "oak/Device.h"
#include <depthai/depthai.hpp>
#include <algorithm>

namespace oak {

void OakDevice::poll() {
    try {
        const auto connected = dai::DeviceBase::getAllConnectedDevices();
        if (connected.empty()) {
            connected_ = false;
            info_ = {};
            return;
        }

        const auto& first = connected[0];
        connected_ = true;
        info_.mxId  = first.getMxId();
        info_.name  = first.toString();

        const auto available = dai::DeviceBase::getAllAvailableDevices();
        info_.available = std::any_of(
            available.begin(), available.end(),
            [&](const dai::DeviceInfo& d) { return d.getMxId() == info_.mxId; }
        );
    } catch (const std::exception& e) {
        connected_ = false;
        info_ = {};
        fprintf(stderr, "Device poll error: %s\n", e.what());
    } catch (...) {
        connected_ = false;
        info_ = {};
    }
}

} // namespace oak
