# bcdec (vendored)

Source: https://github.com/iOrange/bcdec (bcdec.h 0.985), MIT / Unlicense dual licensed
(LICENSE alongside). Single-header BC1-BC7 decoder.

Used ONLY by tests as the spec-conformant reference for BC6H round trips: the engine's own
BC6H encoder (Pipeline/Texture.Compression/Bc6hEncoderImpl.cpp) is validated by decoding
its blocks with a decoder that does not share its bit-packing code. Nothing shipped links it.
