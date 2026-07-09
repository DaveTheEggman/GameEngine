// Draconic GUI - :nine_slice_drawable partition
//
// NineSliceDrawable: draws an image as a nine-patch (fixed corners, stretched edges/
// center) filling the destination rect. Derived from eepp's NinePatch (9 batched textured
// quads); maps to VG's DrawNineSlice. The image is owned elsewhere (non-owning pointer).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:nine_slice_drawable;

import draconic.core;    // Color, Rectangle, Float2, Optional
import draconic.image;   // ImageData, NineSlice
import :rect;
import :draw_context;
import :drawable;

using namespace draconic::core;
namespace core = draconic::core;
namespace image = draconic::image;

export namespace draconic::gui
{
    class NineSliceDrawable : public Drawable
    {
        DRACONIC_OBJECT(NineSliceDrawable, Drawable)
    public:
        const image::ImageData* Image = nullptr;
        image::NineSlice Slices{};
        Color Tint = Color::White;

        NineSliceDrawable() = default;
        NineSliceDrawable(const image::ImageData* image, image::NineSlice slices, Color tint = Color::White) noexcept
            : Image(image), Slices(slices), Tint(tint) {}

        void Draw(DrawContext& ctx, const Rect& dest) override
        {
            if (Image == nullptr)
                return;
            ctx.VG().DrawNineSlice(Image, dest.ToRectangle(),
                core::Rectangle{ 0.0f, 0.0f,
                    static_cast<f32>(Image->Width()), static_cast<f32>(Image->Height()) },
                Slices, Tint);
        }

        [[nodiscard]] Optional<Float2> IntrinsicSize() const override
        {
            if (Image == nullptr)
                return {};
            return core::Float2{ static_cast<f32>(Image->Width()), static_cast<f32>(Image->Height()) };
        }
    };

    DRACONIC_DEFINE_OBJECT(NineSliceDrawable, "draconic::gui")
}
