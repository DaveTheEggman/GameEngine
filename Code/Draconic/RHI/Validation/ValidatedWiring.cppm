/// Deferred implementations that break circular dependencies between
/// ValidatedBackend, ValidatedAdapter, and ValidatedDevice.

module;
#include "Core/Prelude.h"

export module draconic.rhi.validation:wiring;

import draconic.core;
import draconic.rhi;
import :validated_backend;
import :validated_adapter;
import :validated_device;

using namespace draconic::core;

namespace draconic::rhi::validation {

ValidatedAdapter* ValidatedBackend::CreateValidatedAdapter(Adapter* inner) {
    return new ValidatedAdapter(inner);
}

Status ValidatedAdapter::CreateDevice(const DeviceDesc& desc, Device*& out) {
    Device* innerDevice = nullptr;
    Status r = m_inner->CreateDevice(desc, innerDevice);
    if (r != ErrorCode::Ok || !innerDevice) { out = nullptr; return r; }
    out = new ValidatedDevice(innerDevice);
    return ErrorCode::Ok;
}

} // namespace draconic::rhi::validation
