/// Validation wrapper for Adapter.
/// Ported from Sedulous.RHI.Validation/ValidatedAdapter.bf.

module;
#include "Core/Prelude.h"

export module raptor.rhi.validation:validated_adapter;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::validation {

class ValidatedDevice;

class ValidatedAdapter : public Adapter {
public:
    explicit ValidatedAdapter(Adapter* inner) : m_inner(inner) {}

    void GetInfo(AdapterInfo& out) override { m_inner->GetInfo(out); }

    Status CreateDevice(const DeviceDesc& desc, Device*& out) override;

    Adapter* inner() const { return m_inner; }

private:
    Adapter* m_inner;
};

} // namespace raptor::rhi::validation
