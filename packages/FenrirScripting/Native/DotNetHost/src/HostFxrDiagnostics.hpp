#pragma once

#include <hostfxr.h>

#include <string>

namespace Fenrir::Scripting::Detail
{
    // A same-thread, LIFO scope: temporarily replaces this thread's hostfxr writer.
    // Owns message copies and restores both the prior writer and a nested capture.
    class HostFxrDiagnostics
    {
      public:
        explicit HostFxrDiagnostics(hostfxr_set_error_writer_fn setErrorWriter) noexcept;
        ~HostFxrDiagnostics() noexcept;

        HostFxrDiagnostics(const HostFxrDiagnostics&) = delete;
        HostFxrDiagnostics& operator=(const HostFxrDiagnostics&) = delete;
        HostFxrDiagnostics(HostFxrDiagnostics&&) = delete;
        HostFxrDiagnostics& operator=(HostFxrDiagnostics&&) = delete;

        [[nodiscard]] std::string AppendTo(std::string diagnostic) const;

      private:
        static void HOSTFXR_CALLTYPE Capture(const char_t* message) noexcept;
        static thread_local HostFxrDiagnostics* active;

        hostfxr_set_error_writer_fn setErrorWriter;
        hostfxr_error_writer_fn previousWriter = nullptr;
        HostFxrDiagnostics* previousCapture;
        std::string messages;
        bool captureIncomplete = false;
    };
} // namespace Fenrir::Scripting::Detail
