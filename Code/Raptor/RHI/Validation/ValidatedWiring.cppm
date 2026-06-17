/// Deferred implementations that break circular dependencies between
/// ValidatedBackend, ValidatedAdapter, and ValidatedDevice.

module;
#include "Core/Prelude.h"

export module raptor.rhi.validation:wiring;

import raptor.core;
import raptor.rhi;
import :validated_backend;
import :validated_adapter;
import :validated_device;

using namespace raptor::core;

namespace raptor::rhi::validation {

ValidatedAdapter* ValidatedBackend::createValidatedAdapter(Adapter* inner) {
    return new ValidatedAdapter(inner);
}

Status ValidatedAdapter::createDevice(const DeviceDesc& desc, Device*& out) {
    Device* innerDevice = nullptr;
    Status r = inner_->createDevice(desc, innerDevice);
    if (r != ErrorCode::Ok || !innerDevice) { out = nullptr; return r; }
    out = new ValidatedDevice(innerDevice);
    return ErrorCode::Ok;
}

} // namespace raptor::rhi::validation
