// Draconic UI - :theme_palette partition
//
// Seed colors for a theme; controls + theme builders derive consistent state variants from these.
// Ported from Sedulous.UI/src/Styling/ThemePalette.bf. Byte colors -> float (v/255); the default
// member values ARE the Dark palette (Dark() returns the default).

module;
#include "Core/Prelude.h"

export module draconic.ui:theme_palette;

import draconic.core;   // Color

using namespace draconic::core;

export namespace draconic::ui
{
    struct ThemePalette
    {
        /// Primary brand color.
        Color Primary{ 60.0f / 255.0f, 120.0f / 255.0f, 215.0f / 255.0f, 1.0f };
        /// Brighter accent for interactive elements.
        Color PrimaryAccent{ 80.0f / 255.0f, 150.0f / 255.0f, 240.0f / 255.0f, 1.0f };

        /// Window/root background.
        Color Background{ 30.0f / 255.0f, 30.0f / 255.0f, 35.0f / 255.0f, 1.0f };
        /// Panel/card surface background.
        Color Surface{ 42.0f / 255.0f, 44.0f / 255.0f, 54.0f / 255.0f, 1.0f };
        /// Brighter surface for elevated elements.
        Color SurfaceBright{ 55.0f / 255.0f, 58.0f / 255.0f, 70.0f / 255.0f, 1.0f };

        /// Border/divider color.
        Color Border{ 65.0f / 255.0f, 70.0f / 255.0f, 85.0f / 255.0f, 1.0f };

        /// Primary text color.
        Color Text{ 220.0f / 255.0f, 225.0f / 255.0f, 235.0f / 255.0f, 1.0f };
        /// Dimmed/secondary text color.
        Color TextDim{ 140.0f / 255.0f, 150.0f / 255.0f, 170.0f / 255.0f, 1.0f };

        /// Error/danger color.
        Color Error{ 210.0f / 255.0f, 60.0f / 255.0f, 60.0f / 255.0f, 1.0f };
        /// Success color.
        Color Success{ 60.0f / 255.0f, 180.0f / 255.0f, 80.0f / 255.0f, 1.0f };
        /// Warning color.
        Color Warning{ 220.0f / 255.0f, 180.0f / 255.0f, 50.0f / 255.0f, 1.0f };

        /// Default dark palette.
        [[nodiscard]] static ThemePalette Dark() noexcept { return ThemePalette{}; }

        /// Light palette.
        [[nodiscard]] static ThemePalette Light() noexcept
        {
            ThemePalette p;
            p.Primary       = Color{ 40.0f / 255.0f, 100.0f / 255.0f, 200.0f / 255.0f, 1.0f };
            p.PrimaryAccent = Color{ 60.0f / 255.0f, 130.0f / 255.0f, 220.0f / 255.0f, 1.0f };
            p.Background     = Color{ 240.0f / 255.0f, 240.0f / 255.0f, 245.0f / 255.0f, 1.0f };
            p.Surface        = Color{ 255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 1.0f };
            p.SurfaceBright  = Color{ 248.0f / 255.0f, 248.0f / 255.0f, 252.0f / 255.0f, 1.0f };
            p.Border         = Color{ 200.0f / 255.0f, 205.0f / 255.0f, 215.0f / 255.0f, 1.0f };
            p.Text           = Color{ 30.0f / 255.0f, 30.0f / 255.0f, 40.0f / 255.0f, 1.0f };
            p.TextDim        = Color{ 100.0f / 255.0f, 105.0f / 255.0f, 120.0f / 255.0f, 1.0f };
            p.Error          = Color{ 200.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f };
            p.Success        = Color{ 50.0f / 255.0f, 160.0f / 255.0f, 70.0f / 255.0f, 1.0f };
            p.Warning        = Color{ 200.0f / 255.0f, 160.0f / 255.0f, 40.0f / 255.0f, 1.0f };
            return p;
        }
    };
}
