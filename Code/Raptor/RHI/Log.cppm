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
    inline void LogWrite(bool error, const char* utf8)
    {
        const String wide = ToWide(UTF8StringView(reinterpret_cast<const utf8char*>(utf8)));
        if (error) { ConsoleWriteError(wide.AsView()); }
        else       { ConsoleWrite(wide.AsView()); }
        ConsoleWrite(u"\n");
    }

    inline void LogError(const char* message)   { LogWrite(true,  message); }
    inline void LogWarning(const char* message)  { LogWrite(false, message); }
    inline void LogInfo(const char* message)     { LogWrite(false, message); }

    inline void LogErrorf(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap; va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        LogWrite(true, buf);
    }

    inline void LogWarningf(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap; va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        LogWrite(false, buf);
    }
}
