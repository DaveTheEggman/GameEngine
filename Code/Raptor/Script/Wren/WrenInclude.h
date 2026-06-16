// Wren's public header has no extern "C" guards; wrap it so C++ sees C linkage.
// Lives in a header (not the module GMF directly) because GCC requires global
// module fragment content to come purely from preprocessor inclusion.
#ifndef RAPTOR_SCRIPT_WREN_INCLUDE_H
#define RAPTOR_SCRIPT_WREN_INCLUDE_H

extern "C" {
#include <wren.h>
}

#endif // RAPTOR_SCRIPT_WREN_INCLUDE_H
