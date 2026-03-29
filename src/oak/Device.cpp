// src/oak/Device.cpp
#include "oak/Device.h"
#include <depthai/depthai.hpp>

namespace oak {

void OakDevice::poll() {
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
    info_.available = !available.empty();
}

} // namespace oak
