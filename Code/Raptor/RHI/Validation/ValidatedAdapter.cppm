/// Validation wrapper for Adapter.
/// Ported from Sedulous.RHI.Validation/ValidatedAdapter.bf.

export module raptor.rhi.validation:validated_adapter;

import raptor.core;
import raptor.rhi;

export namespace raptor::rhi::validation {

class ValidatedDevice;

class ValidatedAdapter : public Adapter {
public:
    explicit ValidatedAdapter(Adapter* inner) : inner_(inner) {}

    void getInfo(AdapterInfo& out) override { inner_->getInfo(out); }

    Status createDevice(const DeviceDesc& desc, Device*& out) override;

    Adapter* inner() const { return inner_; }

private:
    Adapter* inner_;
};

} // namespace raptor::rhi::validation
