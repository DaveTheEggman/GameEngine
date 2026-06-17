// RHI logging shim. The ported backends use printf-style char* logging
// (logError / logWarning / logErrorf / logWarningf); this routes those to
// Raptor's console (transcoding UTF-8 -> wide). A thin compatibility layer so
// the large backend bodies port unchanged.

module;
#include "Core/Prelude.h"
#include <cstdio>
#include <cstdarg>

export module raptor.rhi:log;

import raptor.core;

using namespace raptor::core;

export namespace raptor::rhi
{
    inline void logWrite(bool error, const char* utf8)
    {
        const String wide = ToWide(UTF8StringView(reinterpret_cast<const utf8char*>(utf8)));
        if (error) { ConsoleWriteError(wide.AsView()); }
        else       { ConsoleWrite(wide.AsView()); }
        ConsoleWrite(u"\n");
    }

    inline void logError(const char* message)   { logWrite(true,  message); }
    inline void logWarning(const char* message)  { logWrite(false, message); }
    inline void logInfo(const char* message)     { logWrite(false, message); }

    inline void logErrorf(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap; va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        logWrite(true, buf);
    }

    inline void logWarningf(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap; va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        logWrite(false, buf);
    }
}
