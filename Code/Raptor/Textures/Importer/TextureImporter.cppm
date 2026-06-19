// Raptor::TexturesImporter — the `raptor.textures.importer` module.
//
// Editor/build-time import: turns image files into a TextureResource (metadata)
// + raw pixel bytes ready for the content DB's "pixels" data stream. The caller
// commits the pair to an Instance (WriteObject + WriteData). 2D, equirectangular
// sky, and 6-face cubemap (packed vertically). Ported from
// Sedulous.Textures.Importer, adapted to Raptor's pixels-as-data-stream model.

module;
#include "Core/Prelude.h"

export module raptor.textures.importer;

import raptor.core;
import raptor.image;
import raptor.image.io;
import raptor.textures;
import raptor.textures.resource;

using namespace raptor::core;

export namespace raptor::textures
{
    namespace img = raptor::image;

    class TextureImporter
    {
    public:
        // Imports a single image file as a 2D texture (3D preset: mips + aniso).
        [[nodiscard]] static Status Import2D(StringView path, img::ImageColorSpace colorSpace,
                                             TextureResource& outResource, Array<u8>& outPixels)
        {
            img::Image image;
            const Status r = img::io::LoadImage(path, image);
            if (!r.IsOk()) { return r; }
            outResource.SetupFor3D();
            Fill2D(outResource, outPixels, image, colorSpace);
            return Status{};
        }

        // Imports a single HDR image as an equirectangular sky (linear, clamped).
        [[nodiscard]] static Status ImportEquirectangular(StringView path,
                                                          TextureResource& outResource, Array<u8>& outPixels)
        {
            img::Image image;
            const Status r = img::io::LoadImage(path, image);
            if (!r.IsOk()) { return r; }
            outResource.SetupForEquirectangularSkybox();
            Fill2D(outResource, outPixels, image, img::ImageColorSpace::Linear);
            return Status{};
        }

        // Imports 6 face images as a cubemap (faces packed vertically, height*6).
        // Face order: +X, -X, +Y, -Y, +Z, -Z. All faces must be square and match.
        [[nodiscard]] static Status ImportCubemap(const StringView (&facePaths)[6], img::ImageColorSpace colorSpace,
                                                  TextureResource& outResource, Array<u8>& outPixels)
        {
            img::Image faces[6];
            for (int i = 0; i < 6; ++i)
            {
                const Status r = img::io::LoadImage(facePaths[i], faces[i]);
                if (!r.IsOk()) { return r; }
            }

            const u32 w = faces[0].Width();
            const u32 h = faces[0].Height();
            if (w != h) { return Status{ ErrorCode::InvalidArgument }; } // faces must be square
            const img::PixelFormat fmt = faces[0].Format();
            for (int i = 1; i < 6; ++i)
            {
                if (faces[i].Width() != w || faces[i].Height() != h || faces[i].Format() != fmt)
                {
                    return Status{ ErrorCode::InvalidArgument }; // all faces must match
                }
            }

            const usize faceBytes = faces[0].PixelData().Size();
            outPixels.Resize(faceBytes * 6);
            for (int i = 0; i < 6; ++i)
            {
                const Span<const u8> src = faces[i].PixelData();
                if (src.Size() != 0) { MemCopy(outPixels.Data() + static_cast<usize>(i) * faceBytes, src.Data(), faceBytes); }
            }

            outResource.SetupForCubemapSkybox();
            outResource.imageWidth = w;          // faceSize
            outResource.imageHeight = h * 6;     // packed
            outResource.imageFormat = fmt;
            outResource.colorSpace = colorSpace;
            return Status{};
        }

        // Probes cubemap face files from one face path (e.g. "sky_px.png" -> the
        // six "sky_*"). Fills `outPaths` and returns Ok only if all six exist.
        [[nodiscard]] static Status DetectCubemapFaces(StringView path, String (&outPaths)[6])
        {
            const StringView dir = PathParent(path);
            const StringView stem = PathStem(path);     // filename without extension
            const StringView ext = PathExtension(path); // includes the dot

            static const StringView kConventions[][6] = {
                { u8"_px", u8"_nx", u8"_py", u8"_ny", u8"_pz", u8"_nz" },
                { u8"px",  u8"nx",  u8"py",  u8"ny",  u8"pz",  u8"nz"  },
                { u8"_posx", u8"_negx", u8"_posy", u8"_negy", u8"_posz", u8"_negz" },
                { u8"_right", u8"_left", u8"_top", u8"_bottom", u8"_front", u8"_back" },
            };

            for (const auto& convention : kConventions)
            {
                int matched = -1;
                for (int i = 0; i < 6; ++i)
                {
                    if (EndsWithCI(stem, convention[i])) { matched = i; break; }
                }
                if (matched < 0) { continue; }

                const StringView prefix = stem.SubStr(0, stem.Size() - convention[matched].Size());

                bool allExist = true;
                for (int i = 0; i < 6; ++i)
                {
                    String name(prefix);
                    name.Append(convention[i]);
                    name.Append(ext);
                    outPaths[i] = dir.IsEmpty() ? name : PathJoin(dir, name.AsView());
                    if (!FileExists(outPaths[i].AsView())) { allExist = false; break; }
                }
                if (allExist) { return Status{}; }
            }
            return Status{ ErrorCode::NotFound };
        }

    private:
        static void Fill2D(TextureResource& out, Array<u8>& outPixels, const img::Image& image, img::ImageColorSpace cs)
        {
            out.imageWidth = image.Width();
            out.imageHeight = image.Height();
            out.imageFormat = image.Format();
            out.colorSpace = cs;
            const Span<const u8> px = image.PixelData();
            outPixels.Resize(px.Size());
            if (px.Size() != 0) { MemCopy(outPixels.Data(), px.Data(), px.Size()); }
        }

        static bool EndsWithCI(StringView s, StringView suffix)
        {
            if (suffix.Size() > s.Size()) { return false; }
            const usize off = s.Size() - suffix.Size();
            for (usize i = 0; i < suffix.Size(); ++i)
            {
                utf8char a = s[off + i], b = suffix[i];
                if (a >= u8'A' && a <= u8'Z') { a = static_cast<utf8char>(a - u8'A' + u8'a'); }
                if (b >= u8'A' && b <= u8'Z') { b = static_cast<utf8char>(b - u8'A' + u8'a'); }
                if (a != b) { return false; }
            }
            return true;
        }
    };
}
