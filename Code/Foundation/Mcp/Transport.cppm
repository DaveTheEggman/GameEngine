// Foundation::Mcp - :transport partition
//
// Line-oriented transport for newline-delimited JSON-RPC (one message per line, NO Content-Length
// headers - that is LSP, not MCP). ITransport is the seam; InMemoryTransport drives tests and
// embedding; StdioTransport is the real stdin/stdout pipe. NOTHING but responses may reach stdout -
// engine logs must go to stderr in the host, or a stray print corrupts the stream. Serve is the v1
// single-threaded loop: read a line, dispatch, write the response.

module;
#include "Core/Prelude.h"

#include <cstdio>

export module foundation.mcp:transport;

import foundation.core;
import :server;

using namespace foundation::core;

export namespace foundation::mcp
{
    class ITransport
    {
    public:
        virtual ~ITransport() = default;
        // Reads the next inbound line (newline stripped) into `out`. Returns false at end of stream.
        [[nodiscard]] virtual bool ReadLine(String& out) = 0;
        // Writes one outbound line; the transport appends the message delimiter.
        virtual void WriteLine(StringView line) = 0;
    };

    // Feed input lines, capture output lines - the test + in-process transport.
    class InMemoryTransport final : public ITransport
    {
        Array<String> m_input;
        usize m_cursor = 0;
        Array<String> m_output;

    public:
        void Push(String line) { m_input.PushBack(Move(line)); }

        [[nodiscard]] bool ReadLine(String& out) override
        {
            if (m_cursor >= m_input.Size())
            {
                return false;
            }
            out = m_input[m_cursor++];
            return true;
        }
        void WriteLine(StringView line) override { m_output.PushBack(String(line)); }

        [[nodiscard]] usize OutputCount() const noexcept { return m_output.Size(); }
        [[nodiscard]] const String& Output(usize index) const { return m_output[index]; }
    };

    // Real stdin/stdout pipe. Reads a full line of any length; writes a line + flush.
    class StdioTransport final : public ITransport
    {
    public:
        [[nodiscard]] bool ReadLine(String& out) override
        {
            out.Clear();
            bool sawAny = false;
            for (;;)
            {
                const int c = std::fgetc(stdin);
                if (c == EOF)
                {
                    return sawAny; // a final line without a trailing newline is still delivered
                }
                sawAny = true;
                if (c == '\n')
                {
                    return true;
                }
                if (c != '\r')
                {
                    out.PushBack(static_cast<char8_t>(c));
                }
            }
        }
        void WriteLine(StringView line) override
        {
            if (line.Size() > 0)
            {
                std::fwrite(line.Data(), 1, line.Size(), stdout);
            }
            std::fputc('\n', stdout);
            std::fflush(stdout);
        }
    };

    // v1 serve loop: single-threaded read -> dispatch -> write, until the input stream ends. A
    // notification (HandleLine returns empty) produces no output line.
    inline void Serve(McpServer& server, ITransport& transport)
    {
        String line;
        while (transport.ReadLine(line))
        {
            Optional<String> response = server.HandleLine(line.AsView());
            if (response.HasValue())
            {
                transport.WriteLine(response.Value().AsView());
            }
        }
    }
}
