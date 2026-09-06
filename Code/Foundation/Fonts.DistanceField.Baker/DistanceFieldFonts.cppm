// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Foundation::Fonts.DistanceField.Baker - foundation.fonts.distancefield.baker:init partition
//
// Registration helper for the distance-field atlas baker.

module;
#include "Core/Prelude.h"

export module foundation.fonts.distancefield.baker:init;

import foundation.core;
import foundation.fonts;
import :baker;

using namespace foundation::core;

export namespace foundation::fonts
{

    class DistanceFieldFonts
    {
    public:
        static void Initialize()
        {
            if (s_baker)
                return;
            s_baker = DefaultAllocator().New<DistanceFieldFontAtlasBaker>();
            FontAtlasBakerFactory::RegisterBaker(s_baker);
        }

        static void Shutdown()
        {
            if (!s_baker)
                return;
            FontAtlasBakerFactory::UnregisterBaker(s_baker);
            DefaultAllocator().Delete(s_baker);
            s_baker = nullptr;
        }

        [[nodiscard]] static bool IsInitialized() { return s_baker != nullptr; }

    private:
        static inline DistanceFieldFontAtlasBaker* s_baker = nullptr;
    };

} // namespace foundation::fonts
