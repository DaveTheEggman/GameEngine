# Color pipeline - what is entered is what is seen

## The problem

PaperKid's grass and houses look pale. Their colours were typed as the colours wanted, and the
renderer draws something lighter and greyer.

- A colour has two readers that disagree about what its numbers mean:
  - The UI draws every colour as sRGB ("Takes the theme's color as authored (sRGB, like every
    UI color)", `UIRuntime.cppm:227`). The colour picker's swatch, its HSV square and its hex
    field therefore show a stored 0.5 as sRGB 0.5, mid grey (`#808080`).
  - The renderer uses the same 0.5 as LINEAR light: a mesh's `color` goes to the GPU untouched
    (`ExtractImpl.cpp:87`, `rd.color = mc.color`), and so do a light's colour, the ambient and
    sky colours, a sprite's tint and a material's `BaseColor`. Linear 0.5 displays as sRGB
    0.735 (`#BCBCBC`).
- Images are already right: a colour image is uploaded in an sRGB GPU format and the hardware
  decodes it to linear on sample (`FormatUtils.cppm:28`). So a texture pixel of 128 and a picked
  tint of `#808080` do not match today: the tint renders far lighter. Images and colours follow
  different rules.
- The pickers: `ColorPicker` (HSV, RGB fields and hex, 0 to 1), shown by `ColorEditor`, the
  property-grid swatch the inspector (`InspectorViewImpl.cpp:782`, :1547, :2146), the material
  page (`MaterialPageImpl.cpp:602`) and the particle page (`ParticleEffectPageImpl.cpp:179`) use;
  and `HDRColorPicker` (an LDR colour times an intensity, giving a `Float4` above 1), which
  nothing in the editor uses yet. Neither converts anything.
- A material has no colour type: a colour parameter is a `Float4` (`MaterialBuilder::Color`
  forwards to `Float4`), and the material page shows EVERY `Float4` with a colour editor, whether
  it is a colour or not.
- Imports: glTF's `baseColorFactor` and `emissiveFactor` are linear by the glTF specification and
  are stored as read (`GltfLoader.cppm:202`, :251); FBX's colours are the DCC's display values,
  also stored as read (`FbxLoader.cppm:256`).

## Proposal

One rule, the one images already follow: **an authored colour is sRGB; the renderer decodes it to
linear once, where it hands the colour to the GPU.** Shading, lighting and blending stay linear.

### 1. Authored colours store sRGB

Every colour field that means "the colour you see" stores the sRGB value: a mesh's or instance
set's colour and tints, a sprite's and decal's tint, a light's colour, the ambient colour, the
sky's horizon, zenith and ground, a material's colour parameters, a particle's colours, UI colours
(already so). The render extraction decodes them (`SrgbToLinear`, `Color.cppm:165`) as it copies
them into render data; nothing upstream of it changes meaning.

Why sRGB in the data and not linear with a converting picker (both look the same on screen):

- The numbers mean the same everywhere: a scene file, the inspector, a hex code, a script's
  `Color(1, 0.5, 0)`, an MCP call and an image editor all agree with what is seen. With linear
  storage every script and tool that writes a colour would have to convert, and the ones that
  forget write PaperKid's pale colours again.
- Colours and images follow one rule, so a tint of `#808080` over white matches a texture of 128.
- A fade between two colours (property animation, a script's lerp) moves evenly to the eye; a
  fade done in linear rushes through the darks.

### 2. Brightness above 1 is an intensity, never a colour

sRGB has no meaning above 1. A colour that has to be brighter than white (a light, emissive, an
HDR sky or particle glow) is an sRGB colour plus a separate linear intensity, as lights already
are (`color` plus `intensity`). Where a field is a single HDR `Float4` today (a material's
`EmissiveColor`, particle colours), it becomes colour plus intensity, edited with the HDR picker
(an sRGB colour plus an intensity, shown in EV as well as the multiplier).

### 3. A colour's role is asked of its field, not listed

- A reflected property says what it is through the property metadata the inspector already reads
  ("range", "visibleWhen", `Reflection.cppm:1785`): `color` = an sRGB colour (the default for a
  `Color` field), `color=linear` for the rare colour that is genuinely linear data (a mask, an
  ID), `color=hdr` for colour plus intensity.
- A material property gains a colour type: `MaterialPropertyType::Color` (sRGB, decoded when the
  uniform is written for the GPU) and `ColorHdr`. `MaterialBuilder::Color` declares it; the
  material page shows a colour editor only for these, and plain number fields for any other
  `Float4`. Shaders keep receiving a linear `float4`.
- The extraction decodes by that role, so a new colour field gets the rule by being declared, not
  by being added to a list.

### 4. One picker behaviour

The swatch, the HSV square, the RGB fields and the hex field show and edit the stored sRGB value;
since storage is sRGB, the pickers need no conversion of their own, only the HDR picker's
intensity row. An optional "linear" readout shows the decoded value for debugging.

### 5. Imports keep their look

- glTF: `baseColorFactor` and `emissiveFactor` are encoded linear to sRGB on import (emissive
  above 1 split into colour and intensity).
- FBX: stored as read (its values are already display values); checked against a reference model
  rendered in Blender.
- Images: unchanged. The texture import's colour space setting stays the switch between colour
  imagery (sRGB) and data (normal, roughness, masks: linear).

### 6. Existing content

Content authored so far renders darker and richer once, because its colours finally mean what was
typed. Sky Hopper and PaperKid are retuned (exposure, ambient, sky) after the change and measured
the way PaperKid's tuning measures (`Tools/look.py`); the built-in defaults whose values were
chosen by eye against the linear reading (the default ambient and sky colours,
`RenderComponents.cppm:518`, :538) are re-picked so a new scene still looks as intended.

## Tests

- A picked tint matches a texture: a quad tinted `#808080` over a white texture and a quad with a
  128-grey texture render to the same pixel value (render test).
- Extraction decodes by role: an sRGB colour field reaches render data as `SrgbToLinear` of the
  stored value; a `color=linear` field arrives unchanged; an HDR field arrives as decoded colour
  times intensity.
- Materials: a `Color` property is decoded when written for the GPU, a `Float4` is not; the
  material page shows a colour editor only for colour properties.
- glTF import: a `baseColorFactor` of linear 0.2140 is stored as sRGB 0.5 (within rounding).
- The pickers: the hex shown for a stored colour is its sRGB bytes; the HDR picker round-trips
  colour plus intensity, and its EV readout matches the multiplier.
- Serialization: the colour-plus-intensity split reads older data (an HDR `Float4` becomes its
  normalized colour and intensity).

## Not in scope

Wide-gamut or HDR display output; a colour-managed (ACES/OCIO) authoring pipeline; changes to the
tonemapper. The DDS item in the backlog is unrelated (a test's bit shift, not a colour shift).
