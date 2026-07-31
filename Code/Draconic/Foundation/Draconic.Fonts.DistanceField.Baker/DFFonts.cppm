// Draconic::FontsDFBaker - draconic.fonts.distancefield.baker:init partition
//
// Registration helper for the distance-field atlas baker.

module;
#include "Draconic.Core/Prelude.h"

export module draconic.fonts.distancefield.baker:init;

import draconic.core;
import draconic.fonts;
import draconic.fonts.io;
import :baker;

using namespace draconic::core;

export namespace draconic::fonts
{

    class DFFonts
    {
    public:
        static void Initialize()
        {
            if (s_baker)
                return;
            s_baker = DefaultAllocator().New<DFFontAtlasBaker>();
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
        static inline DFFontAtlasBaker* s_baker = nullptr;
    };

} // namespace draconic::fonts
