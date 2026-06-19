/// Skin data for skeletal animation.
/// Ported from Sedulous.Models/ModelSkin.bf.

module;
#include "Core/Prelude.h"

#include <string>
#include <vector>

export module raptor.model:model_skin;

import raptor.core;

using namespace raptor::core;

export namespace raptor::model {

/// Skin data binding joints to inverse bind matrices.
class ModelSkin {
public:
    ModelSkin() = default;
    ~ModelSkin() = default;

    [[nodiscard]] WideStringView name() const { return WideStringView(m_name.Data(), m_name.Size()); }
    void setName(WideStringView n) { m_name = WideString(n); }

    /// Add a joint to the skin.
    void addJoint(i32 boneIndex, Mat4 inverseBindMatrix) {
        m_joints.PushBack(boneIndex);
        m_inverseBindMatrices.PushBack(inverseBindMatrix);
    }

    [[nodiscard]] Span<const i32> joints() const {
        return Span<const i32>(m_joints.Data(), m_joints.Size());
    }
    [[nodiscard]] Span<i32> joints() {
        return Span<i32>(m_joints.Data(), m_joints.Size());
    }

    [[nodiscard]] Span<const Mat4> inverseBindMatrices() const {
        return Span<const Mat4>(m_inverseBindMatrices.Data(), m_inverseBindMatrices.Size());
    }
    [[nodiscard]] Span<Mat4> inverseBindMatrices() {
        return Span<Mat4>(m_inverseBindMatrices.Data(), m_inverseBindMatrices.Size());
    }

    // -- Public fields --

    /// Index of the skeleton root bone (-1 if not specified).
    i32 skeletonRootIndex = -1;

private:
    WideString m_name;
    Array<i32> m_joints;
    Array<Mat4> m_inverseBindMatrices;
};

} // namespace raptor::model
