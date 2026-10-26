#include "FenrirWindowScripting/WindowScripting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/Enumerator.hpp"
#include "FenrirWindow/WindowEvents.hpp"
#include "FenrirWindow/WindowTypes.hpp"
#include "Utf16.hpp"
#include "WindowScriptingState.hpp"

// Everything C# sees of the window is generated (api/FenrirWindow.binding.toml): the events' and the size's mappings,
// checked against the Fenrir.Window contract whenever a script assembly loads, and from the native functions below
// (described into api/FenrirWindow.natives.json) their C# declarations, WindowNative, and this registration of them
// all.
namespace Fenrir::Generated
{
    void RegisterWindow(ScriptingExtension& extension);
} // namespace Fenrir::Generated

namespace Fenrir
{
    // The native functions behind Fenrir.Window's Window, each named as C# calls it. tools/describe-api reads them from
    // this file, in this order, and Iris.Bindings generates their C# declarations (Fenrir.Window.WindowNative) and
    // their registration from that. With no such window (no Window installed, the window not made yet, or an id that
    // names none) each does nothing and returns false, zero or an empty value, warning once per call site in a debug
    // build. Named apart from Window's own functions, and not in an anonymous namespace: the generated registration
    // names them from another file.
    namespace WindowNatives
    {
        /** @brief Whether `window` has been made. */
        bool Exists(const CallContext& context, WindowId window)
        {
            return Window::Exists(context.GetApp(), window);
        }

        /**
         * @brief `window`'s size in logical units.
         * @param[out] size The size; empty with no such window.
         */
        void Size(const CallContext& context, WindowScripting& windows, WindowId window, WindowSize* size)
        {
            const bool made = windows.Made(context, window, "Window.Size");
            if (size != nullptr)
            {
                *size = made ? Window::LogicalSize(context.GetApp(), window) : WindowSize{};
            }
        }

        /**
         * @brief `window`'s size in pixels.
         * @param[out] size The size; empty with no such window.
         */
        void PixelSize(const CallContext& context, WindowScripting& windows, WindowId window, WindowSize* size)
        {
            const bool made = windows.Made(context, window, "Window.PixelSize");
            if (size != nullptr)
            {
                *size = made ? Window::PixelSize(context.GetApp(), window) : WindowSize{};
            }
        }

        /** @brief `window`'s pixels per logical unit. */
        float ContentScale(const CallContext& context, WindowScripting& windows, WindowId window)
        {
            return windows.Made(context, window, "Window.ContentScale") ? Window::ContentScale(context.GetApp(), window)
                                                                        : 0.0f;
        }

        /** @brief Whether `window` is minimised: nothing should be drawn to it. */
        bool IsMinimised(const CallContext& context, WindowScripting& windows, WindowId window)
        {
            return windows.Made(context, window, "Window.IsMinimised") && Window::IsMinimised(context.GetApp(), window);
        }

        /** @brief Whether `window` has the input focus. */
        bool IsFocused(const CallContext& context, WindowScripting& windows, WindowId window)
        {
            return windows.Made(context, window, "Window.IsFocused") && Window::IsFocused(context.GetApp(), window);
        }

        /** @brief How the cursor behaves over `window`, as last set. */
        CursorMode GetCursorMode(const CallContext& context, WindowScripting& windows, WindowId window)
        {
            return windows.Made(context, window, "Window.GetCursorMode")
                       ? Window::GetCursorMode(context.GetApp(), window)
                       : CursorMode::Normal;
        }

        /**
         * @brief Sets how the cursor behaves over `window`.
         * @return Whether it was set: false with no such window, or if the platform refuses.
         */
        bool SetCursorMode(const CallContext& context, WindowScripting& windows, WindowId window, CursorMode mode)
        {
            if (!IsEnumerator<CursorMode::Captured>(mode))
            {
                windows.warnings.Warn(context, "Window.SetCursorMode: no such cursor mode, so it does nothing");
                return false;
            }
            return windows.Made(context, window, "Window.SetCursorMode") &&
                   Window::SetCursorMode(context.GetApp(), window, mode);
        }

        /**
         * @brief Sets the cursor shown over `window`.
         * @return Whether it was set: false with no such window, or if the platform refuses.
         */
        bool SetCursorShape(const CallContext& context, WindowScripting& windows, WindowId window, CursorShape shape)
        {
            if (!IsEnumerator<CursorShape::Hidden>(shape))
            {
                windows.warnings.Warn(context, "Window.SetCursorShape: no such cursor shape, so it does nothing");
                return false;
            }
            return windows.Made(context, window, "Window.SetCursorShape") &&
                   Window::SetCursorShape(context.GetApp(), window, shape);
        }

        /**
         * @brief Sets `window`'s title.
         * @return Whether it was set: false with no such window, or if the platform refuses.
         */
        bool SetTitle(const CallContext& context, WindowScripting& windows, WindowId window, std::string_view title)
        {
            return windows.Made(context, window, "Window.SetTitle") &&
                   Window::SetTitle(context.GetApp(), window, title);
        }

        /**
         * @brief Writes the system clipboard's text, as UTF-16, to `text` and returns its whole length in chars.
         * @param text Where to write at most `capacity` chars.
         */
        std::int32_t GetClipboard(const CallContext& context, WindowScripting& windows, char16_t* text,
                                  std::int32_t capacity)
        {
            if (!windows.Made(context, Window::MainWindow(), "Window.GetClipboard"))
            {
                return 0;
            }
            const std::u16string utf16 = Detail::Utf8ToUtf16(Window::ClipboardText(context.GetApp()));
            const std::size_t room = text == nullptr || capacity < 0 ? 0 : static_cast<std::size_t>(capacity);
            std::copy_n(utf16.begin(), std::min(room, utf16.size()), text);
            return static_cast<std::int32_t>(utf16.size());
        }

        /**
         * @brief Puts `text` on the system clipboard.
         * @return Whether it was put there: false with no window, or if the platform refuses.
         */
        bool SetClipboard(const CallContext& context, WindowScripting& windows, std::string_view text)
        {
            return windows.Made(context, Window::MainWindow(), "Window.SetClipboard") &&
                   Window::SetClipboardText(context.GetApp(), text);
        }

        /**
         * @brief Asks for `window` to close, as its close button does.
         * @return Whether the request was made: false with no such window.
         */
        bool RequestClose(const CallContext& context, WindowScripting& windows, WindowId window)
        {
            return windows.Made(context, window, "Window.RequestClose") &&
                   Window::RequestClose(context.GetApp(), window);
        }
    } // namespace WindowNatives

    Fenrir::ScriptingExtension Window::ScriptingExtension()
    {
        Fenrir::ScriptingExtension extension;
        extension.name = "FenrirWindow";
        extension.SetState(std::make_shared<WindowScripting>());
        extension.sharedAssemblies = {"Fenrir.Window.dll"};
        Generated::RegisterWindow(extension);
        return extension;
    }
} // namespace Fenrir
