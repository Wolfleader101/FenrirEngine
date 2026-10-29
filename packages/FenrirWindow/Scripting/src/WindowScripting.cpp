#include "FenrirWindowScripting/WindowScripting.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/Enumerator.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"
#include "FenrirWindow/WindowEvents.hpp"
#include "FenrirWindow/WindowTypes.hpp"
#include "Generated/FenrirWindowNatives.hpp"
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
    // build under the native's name. Each trusts the generated declaration (ADR-0045): its `out` pointer is never null
    // and the registration has zeroed it before the native runs. Named apart from Window's own functions, and not in an
    // anonymous namespace: the generated natives header declares them.
    namespace WindowNatives
    {
        /**
         * @brief Whether `window` has been made.
         * @facade Window.Exists
         */
        bool Exists(const CallContext& context, WindowId window)
        {
            return Window::Exists(context.GetApp(), window);
        }

        /**
         * @brief `window`'s size in logical units.
         * @param[out] size The size; empty with no such window.
         * @facade Window.Size returns size
         */
        void Size(const CallContext& context, WindowScripting& windows, WindowId window, WindowSize* size)
        {
            if (windows.Made(context, window))
            {
                *size = Window::LogicalSize(context.GetApp(), window);
            }
        }

        /**
         * @brief `window`'s size in pixels.
         * @param[out] size The size; empty with no such window.
         * @facade Window.PixelSize returns size
         */
        void PixelSize(const CallContext& context, WindowScripting& windows, WindowId window, WindowSize* size)
        {
            if (windows.Made(context, window))
            {
                *size = Window::PixelSize(context.GetApp(), window);
            }
        }

        /**
         * @brief `window`'s pixels per logical unit.
         * @facade Window.ContentScale
         */
        float ContentScale(const CallContext& context, WindowScripting& windows, WindowId window)
        {
            return windows.Made(context, window) ? Window::ContentScale(context.GetApp(), window) : 0.0f;
        }

        /**
         * @brief Whether `window` is minimised: nothing should be drawn to it.
         * @facade Window.IsMinimised
         */
        bool IsMinimised(const CallContext& context, WindowScripting& windows, WindowId window)
        {
            return windows.Made(context, window) && Window::IsMinimised(context.GetApp(), window);
        }

        /**
         * @brief Whether `window` has the input focus.
         * @facade Window.IsFocused
         */
        bool IsFocused(const CallContext& context, WindowScripting& windows, WindowId window)
        {
            return windows.Made(context, window) && Window::IsFocused(context.GetApp(), window);
        }

        /**
         * @brief How the cursor behaves over `window`, as last set.
         * @facade Window.GetCursorMode
         */
        CursorMode GetCursorMode(const CallContext& context, WindowScripting& windows, WindowId window)
        {
            return windows.Made(context, window) ? Window::GetCursorMode(context.GetApp(), window) : CursorMode::Normal;
        }

        /**
         * @brief Sets how the cursor behaves over `window`.
         * @return Whether it was set: false with no such window, or if the platform refuses.
         * @facade Window.SetCursorMode
         */
        bool SetCursorMode(const CallContext& context, WindowScripting& windows, WindowId window, CursorMode mode)
        {
            if (!IsEnumerator<CursorMode::Captured>(mode))
            {
                NativeGuard(context, windows.warnings).Warn(std::string(WindowScripting::NoCursorMode));
                return false;
            }
            return windows.Made(context, window) && Window::SetCursorMode(context.GetApp(), window, mode);
        }

        /**
         * @brief Sets the cursor shown over `window`.
         * @return Whether it was set: false with no such window, or if the platform refuses.
         * @facade Window.SetCursorShape
         */
        bool SetCursorShape(const CallContext& context, WindowScripting& windows, WindowId window, CursorShape shape)
        {
            if (!IsEnumerator<CursorShape::Hidden>(shape))
            {
                NativeGuard(context, windows.warnings).Warn(std::string(WindowScripting::NoCursorShape));
                return false;
            }
            return windows.Made(context, window) && Window::SetCursorShape(context.GetApp(), window, shape);
        }

        /**
         * @brief Sets `window`'s title.
         * @return Whether it was set: false with no such window, or if the platform refuses.
         * @facade Window.SetTitle
         */
        bool SetTitle(const CallContext& context, WindowScripting& windows, WindowId window, std::string_view title)
        {
            return windows.Made(context, window) && Window::SetTitle(context.GetApp(), window, title);
        }

        /**
         * @brief Writes the system clipboard's text, as UTF-16, to `text` and returns its whole length in chars: as
         * much as fits is written, so a result larger than the span says how large a span would hold it. Zero when
         * the clipboard holds no text.
         * @param text Where to write at most `capacity` chars.
         * @facade Window.GetClipboard
         */
        std::int32_t GetClipboard(const CallContext& context, WindowScripting& windows, char16_t* text,
                                  std::int32_t capacity)
        {
            if (!windows.Made(context, Window::MainWindow()))
            {
                return 0;
            }
            return WriteText(text, capacity, Utf8ToUtf16(Window::ClipboardText(context.GetApp())));
        }

        /**
         * @brief Puts `text` on the system clipboard.
         * @return Whether it was put there: false with no window, or if the platform refuses.
         * @facade Window.SetClipboard
         */
        bool SetClipboard(const CallContext& context, WindowScripting& windows, std::string_view text)
        {
            return windows.Made(context, Window::MainWindow()) && Window::SetClipboardText(context.GetApp(), text);
        }

        /**
         * @brief Asks for `window` to close, as its close button does: a `WindowCloseRequestedEvent` is sent and, for
         * the main window when the app stops on its close, the app stops. The window itself stays.
         * @return Whether the request was made: false with no such window.
         * @facade Window.RequestClose
         */
        bool RequestClose(const CallContext& context, WindowScripting& windows, WindowId window)
        {
            return windows.Made(context, window) && Window::RequestClose(context.GetApp(), window);
        }
    } // namespace WindowNatives

    Fenrir::ScriptingExtension Window::ScriptingExtension()
    {
        return Generated::MakeWindowExtension(std::make_shared<WindowScripting>());
    }
} // namespace Fenrir
