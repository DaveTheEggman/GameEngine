// Foundation::Fonts.TTF - the `foundation.fonts.ttf` module.
//
// TrueType/OpenType backend over stb_truetype: a TrueTypeFont (IFont), a
// stb-packed TrueTypeFontAtlas (IFontAtlas), the matching parser + atlas baker,
// a text shaper, the registration helper (TrueTypeFonts), and a VFS-aware
// IFontService. Ported from Sedulous.Fonts.TTF - its own library, matching
// Sedulous. One named module composed of partitions.

export module foundation.fonts.ttf;

export import :common;
export import :font;
export import :atlas;
export import :parser;
export import :atlas_baker;
export import :text_shaper;
export import :init;
export import :service;
