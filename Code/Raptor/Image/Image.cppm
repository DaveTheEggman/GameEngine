/// Image — owns a CPU-side pixel buffer with manipulation methods.
/// Implements ImageData so it can be passed to anything accepting the base type.
/// Ported from Sedulous.Images.Image.

module;
#include "Core/Prelude.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

export module raptor.image:image;

import raptor.core;
import :pixel_format;
import :image_data;

using namespace raptor::core;

export namespace raptor::image {

/// Simple RGBA color value.
struct Color {
    u8 r = 0, g = 0, b = 0, a = 255;

    static constexpr Color Black()       { return { 0, 0, 0, 255 }; }
    static constexpr Color White()       { return { 255, 255, 255, 255 }; }
    static constexpr Color Transparent() { return { 0, 0, 0, 0 }; }
};

/// Image that owns a pixel buffer. Inherits ImageData for polymorphic use.
/// Supports pixel access, flips, format conversion, and procedural factories.
class Image : public ImageData {
public:
    Image() = default;

    Image(u32 w, u32 h, PixelFormat fmt, Span<const u8> srcData = {})
        : m_width(w), m_height(h), m_format(fmt)
    {
        usize needed = DataSize();
        m_data.Resize(needed);
        if (srcData.Size() >= needed)
            std::memcpy(m_data.Data(), srcData.Data(), needed);
        else
            Clear();
    }

    Image(const Image&) = default;
    Image(Image&&) noexcept = default;
    Image& operator=(const Image&) = default;
    Image& operator=(Image&&) noexcept = default;

    // ---- ImageData interface ----
    [[nodiscard]] u32 Width()  const override { return m_width; }
    [[nodiscard]] u32 Height() const override { return m_height; }
    [[nodiscard]] PixelFormat Format() const override { return m_format; }
    [[nodiscard]] ImageColorSpace ColorSpace() const override { return m_colorSpace; }
    [[nodiscard]] Span<const u8> PixelData() const override { return { m_data.Data(), m_data.Size() }; }

    // ---- Mutable access ----
    [[nodiscard]] Span<u8> PixelDataMut() { return { m_data.Data(), m_data.Size() }; }
    [[nodiscard]] u32 PixelCount() const { return m_width * m_height; }
    [[nodiscard]] usize DataSize() const { return static_cast<usize>(PixelCount()) * BytesPerPixel(m_format); }
    void SetColorSpace(ImageColorSpace cs) { m_colorSpace = cs; }

    /// Replace dimensions, format, and pixel data in-place (hot-reload).
    void ReplaceData(u32 w, u32 h, PixelFormat fmt, Span<const u8> src) {
        m_width = w; m_height = h; m_format = fmt;
        usize needed = DataSize();
        m_data.Resize(needed);
        usize copy = Min(needed, src.Size());
        if (copy > 0) std::memcpy(m_data.Data(), src.Data(), copy);
        if (copy < needed) std::memset(m_data.Data() + copy, 0, needed - copy);
    }

    // ---- Pixel access ----

    void Clear() {
        if (HasAlpha(m_format))
            FillColor(Color::Transparent());
        else
            std::memset(m_data.Data(), 0, m_data.Size());
    }

    void FillColor(Color c) {
        u32 bpp = BytesPerPixel(m_format);
        for (usize i = 0; i < m_data.Size(); i += bpp) {
            switch (m_format) {
            case PixelFormat::R8:
                m_data[i] = static_cast<u8>((c.r + c.g + c.b) / 3);
                break;
            case PixelFormat::RG8:
                m_data[i] = c.r; m_data[i+1] = c.g;
                break;
            case PixelFormat::RGB8:
                m_data[i] = c.r; m_data[i+1] = c.g; m_data[i+2] = c.b;
                break;
            case PixelFormat::RGBA8:
                m_data[i] = c.r; m_data[i+1] = c.g; m_data[i+2] = c.b; m_data[i+3] = c.a;
                break;
            case PixelFormat::BGR8:
                m_data[i] = c.b; m_data[i+1] = c.g; m_data[i+2] = c.r;
                break;
            case PixelFormat::BGRA8:
                m_data[i] = c.b; m_data[i+1] = c.g; m_data[i+2] = c.r; m_data[i+3] = c.a;
                break;
            default: break;
            }
        }
    }

    [[nodiscard]] Color GetPixel(u32 x, u32 y) const {
        if (x >= m_width || y >= m_height) return Color::Black();
        usize off = PixelOffset(x, y);
        switch (m_format) {
        case PixelFormat::R8:    { u8 g = m_data[off]; return {g,g,g,255}; }
        case PixelFormat::RGB8:  return {m_data[off], m_data[off+1], m_data[off+2], 255};
        case PixelFormat::RGBA8: return {m_data[off], m_data[off+1], m_data[off+2], m_data[off+3]};
        case PixelFormat::BGR8:  return {m_data[off+2], m_data[off+1], m_data[off], 255};
        case PixelFormat::BGRA8: return {m_data[off+2], m_data[off+1], m_data[off], m_data[off+3]};
        default: return Color::Black();
        }
    }

    void SetPixel(u32 x, u32 y, Color c) {
        if (x >= m_width || y >= m_height) return;
        usize off = PixelOffset(x, y);
        switch (m_format) {
        case PixelFormat::R8:    m_data[off] = static_cast<u8>((c.r+c.g+c.b)/3); break;
        case PixelFormat::RGB8:  m_data[off]=c.r; m_data[off+1]=c.g; m_data[off+2]=c.b; break;
        case PixelFormat::RGBA8: m_data[off]=c.r; m_data[off+1]=c.g; m_data[off+2]=c.b; m_data[off+3]=c.a; break;
        case PixelFormat::BGR8:  m_data[off]=c.b; m_data[off+1]=c.g; m_data[off+2]=c.r; break;
        case PixelFormat::BGRA8: m_data[off]=c.b; m_data[off+1]=c.g; m_data[off+2]=c.r; m_data[off+3]=c.a; break;
        default: break;
        }
    }

    // ---- Flips ----

    void FlipVertical() {
        u32 rowSize = m_width * BytesPerPixel(m_format);
        Array<u8> tmp(rowSize);
        for (u32 y = 0; y < m_height / 2; ++y) {
            u8* top = m_data.Data() + y * rowSize;
            u8* bot = m_data.Data() + (m_height - 1 - y) * rowSize;
            std::memcpy(tmp.Data(), top, rowSize);
            std::memcpy(top, bot, rowSize);
            std::memcpy(bot, tmp.Data(), rowSize);
        }
    }

    void FlipHorizontal() {
        u32 bpp = BytesPerPixel(m_format);
        Array<u8> tmp(bpp);
        for (u32 y = 0; y < m_height; ++y) {
            for (u32 x = 0; x < m_width / 2; ++x) {
                u8* left  = m_data.Data() + PixelOffset(x, y);
                u8* right = m_data.Data() + PixelOffset(m_width - 1 - x, y);
                std::memcpy(tmp.Data(), left, bpp);
                std::memcpy(left, right, bpp);
                std::memcpy(right, tmp.Data(), bpp);
            }
        }
    }

    // ---- Format conversion ----

    [[nodiscard]] Image ConvertFormat(PixelFormat newFmt) const {
        if (newFmt == m_format) return *this;
        Image out(m_width, m_height, newFmt);
        for (u32 y = 0; y < m_height; ++y)
            for (u32 x = 0; x < m_width; ++x)
                out.SetPixel(x, y, GetPixel(x, y));
        return out;
    }

    // ---- Factories ----

    static Image CreateSolidColor(u32 w, u32 h, Color c, PixelFormat fmt = PixelFormat::RGBA8) {
        Image img(w, h, fmt);
        img.FillColor(c);
        return img;
    }

    static Image CreateCheckerboard(u32 size = 256, Color c1 = Color::White(), Color c2 = Color::Black(),
                                     u32 checkSize = 32, PixelFormat fmt = PixelFormat::RGBA8) {
        Image img(size, size, fmt);
        for (u32 y = 0; y < size; ++y)
            for (u32 x = 0; x < size; ++x)
                img.SetPixel(x, y, ((x/checkSize + y/checkSize) % 2 == 0) ? c1 : c2);
        return img;
    }

    static Image CreateGradient(u32 w, u32 h, Color top, Color bottom, PixelFormat fmt = PixelFormat::RGBA8) {
        Image img(w, h, fmt);
        for (u32 y = 0; y < h; ++y) {
            f32 t = static_cast<f32>(y) / static_cast<f32>(h > 1 ? h - 1 : 1);
            Color c{
                static_cast<u8>(top.r + t * (bottom.r - top.r)),
                static_cast<u8>(top.g + t * (bottom.g - top.g)),
                static_cast<u8>(top.b + t * (bottom.b - top.b)),
                static_cast<u8>(top.a + t * (bottom.a - top.a)),
            };
            for (u32 x = 0; x < w; ++x) img.SetPixel(x, y, c);
        }
        return img;
    }

private:
    [[nodiscard]] usize PixelOffset(u32 x, u32 y) const {
        return static_cast<usize>(y * m_width + x) * BytesPerPixel(m_format);
    }

    u32 m_width = 0, m_height = 0;
    PixelFormat m_format = PixelFormat::RGBA8;
    ImageColorSpace m_colorSpace = ImageColorSpace::Srgb;
    Array<u8> m_data;
};

} // namespace raptor::image
