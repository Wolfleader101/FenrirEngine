#include "HostFxrDiagnostics.hpp"

#include "StringEncoding.hpp"

namespace Iris::Detail
{
    thread_local HostFxrDiagnostics* HostFxrDiagnostics::active = nullptr;

    HostFxrDiagnostics::HostFxrDiagnostics(hostfxr_set_error_writer_fn writerSetter) noexcept
        : setErrorWriter(writerSetter), previousCapture(active)
    {
        active = this;
        previousWriter = setErrorWriter(&Capture);
    }

    HostFxrDiagnostics::~HostFxrDiagnostics() noexcept
    {
        setErrorWriter(previousWriter);
        active = previousCapture;
    }

    void HOSTFXR_CALLTYPE HostFxrDiagnostics::Capture(const char_t* message) noexcept
    {
        if (active == nullptr || message == nullptr || *message == char_t{})
        {
            return;
        }
        try
        {
            // hostfxr provides a null-terminated message borrowed only during this callback.
            const std::string text = HostStringToUtf8(HostStringView(message));
            if (!active->messages.empty() && active->messages.back() != '\n')
            {
                active->messages += '\n';
            }
            active->messages += text;
        }
        catch (...)
        {
            // Encoding and allocation failures must never escape the callback's C ABI.
            active->captureIncomplete = true;
        }
    }

    std::string HostFxrDiagnostics::AppendTo(std::string diagnostic) const
    {
        if (!messages.empty())
        {
            if (!diagnostic.empty() && diagnostic.back() != '\n')
            {
                diagnostic += '\n';
            }
            diagnostic += messages;
        }
        if (captureIncomplete)
        {
            if (!diagnostic.empty() && diagnostic.back() != '\n')
            {
                diagnostic += '\n';
            }
            diagnostic += "Some hostfxr diagnostic messages could not be captured.";
        }
        return diagnostic;
    }
} // namespace Iris::Detail
