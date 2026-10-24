#include "FenrirWindowScripting/WindowScripting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>

#include "FenrirApp/App.hpp"
#include "FenrirInputScripting/InputMappings.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"
#include "FenrirWindow/WindowEvents.hpp"
#include "FenrirWindow/WindowTypes.hpp"
#include "Utf16.hpp"

// How the window's events look to C#: each mapping is checked against its mirror in the Fenrir.Window contract
// (Managed/Fenrir.Window) whenever a script assembly loads, so a drift fails the load and names the field.

template <>
struct Iris::StructMapping<Fenrir::WindowSize>
{
    static constexpr std::string_view typeName = "Fenrir.Window.WindowSize";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::WindowSize, width, "Width"),
                                                      IRIS_FIELD_AS(Fenrir::WindowSize, height, "Height"));
};

template <>
struct Iris::StructMapping<Fenrir::WindowResizedEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Window.WindowResizedEvent";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::WindowResizedEvent, window, "Window"),
                           IRIS_FIELD_AS(Fenrir::WindowResizedEvent, logicalSize, "LogicalSize"),
                           IRIS_FIELD_AS(Fenrir::WindowResizedEvent, pixelSize, "PixelSize"),
                           IRIS_FIELD_AS(Fenrir::WindowResizedEvent, contentScale, "ContentScale"));
};

template <>
struct Iris::StructMapping<Fenrir::WindowCloseRequestedEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Window.WindowCloseRequestedEvent";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::WindowCloseRequestedEvent, window, "Window"));
};

namespace Fenrir
{
    namespace
    {
        // A warning logged once for each place in this file that notices a call doing nothing, in debug builds only.
        void WarnOnce(const CallContext& context, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
#ifndef NDEBUG
            // Keyed by the message too: one call site can notice more than one reason.
            static std::mutex mutex;
            static std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (!warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                {
                    return;
                }
            }
            context.GetApp().Logger()->Warn("[window scripting] {} (warned once)", message);
#else
            (void)context;
            (void)message;
            (void)where;
#endif
        }

        // Whether `window` has been made in the app (so there is a Window, too); warns once for the caller's call site
        // if not.
        bool Made(const CallContext& context, std::uint32_t window, std::string_view function,
                  std::source_location where = std::source_location::current())
        {
            if (Window::Exists(context.GetApp(), WindowId{window}))
            {
                return true;
            }
            WarnOnce(context,
                     std::string(function) + ": the app has no such window (no Window installed, the window not made "
                                             "yet, or an id that names none), so it does nothing",
                     where);
            return false;
        }

        // The native functions, named apart from Window's own functions, which Window::ScriptingExtension would find
        // first.
        namespace Native
        {
            bool Exists(const CallContext& context, std::uint32_t window)
            {
                return Window::Exists(context.GetApp(), WindowId{window});
            }

            void Size(const CallContext& context, std::uint32_t window, WindowSize* size)
            {
                const bool made = Made(context, window, "Window.Size");
                if (size != nullptr)
                {
                    *size = made ? Window::LogicalSize(context.GetApp(), WindowId{window}) : WindowSize{};
                }
            }

            void PixelSize(const CallContext& context, std::uint32_t window, WindowSize* size)
            {
                const bool made = Made(context, window, "Window.PixelSize");
                if (size != nullptr)
                {
                    *size = made ? Window::PixelSize(context.GetApp(), WindowId{window}) : WindowSize{};
                }
            }

            float ContentScale(const CallContext& context, std::uint32_t window)
            {
                return Made(context, window, "Window.ContentScale")
                           ? Window::ContentScale(context.GetApp(), WindowId{window})
                           : 0.0f;
            }

            bool IsMinimised(const CallContext& context, std::uint32_t window)
            {
                return Made(context, window, "Window.IsMinimised") &&
                       Window::IsMinimised(context.GetApp(), WindowId{window});
            }

            bool IsFocused(const CallContext& context, std::uint32_t window)
            {
                return Made(context, window, "Window.IsFocused") &&
                       Window::IsFocused(context.GetApp(), WindowId{window});
            }

            std::uint8_t GetCursorMode(const CallContext& context, std::uint32_t window)
            {
                const CursorMode mode = Made(context, window, "Window.GetCursorMode")
                                            ? Window::GetCursorMode(context.GetApp(), WindowId{window})
                                            : CursorMode::Normal;
                return static_cast<std::uint8_t>(mode);
            }

            bool SetCursorMode(const CallContext& context, std::uint32_t window, std::uint8_t mode)
            {
                if (mode > static_cast<std::uint8_t>(CursorMode::Captured))
                {
                    WarnOnce(context, "Window.SetCursorMode: no such cursor mode, so it does nothing");
                    return false;
                }
                return Made(context, window, "Window.SetCursorMode") &&
                       Window::SetCursorMode(context.GetApp(), WindowId{window}, static_cast<CursorMode>(mode));
            }

            bool SetCursorShape(const CallContext& context, std::uint32_t window, std::uint8_t shape)
            {
                if (shape > static_cast<std::uint8_t>(CursorShape::Hidden))
                {
                    WarnOnce(context, "Window.SetCursorShape: no such cursor shape, so it does nothing");
                    return false;
                }
                return Made(context, window, "Window.SetCursorShape") &&
                       Window::SetCursorShape(context.GetApp(), WindowId{window}, static_cast<CursorShape>(shape));
            }

            bool SetTitle(const CallContext& context, std::uint32_t window, std::string_view title)
            {
                return Made(context, window, "Window.SetTitle") &&
                       Window::SetTitle(context.GetApp(), WindowId{window}, title);
            }

            std::int32_t GetClipboard(const CallContext& context, char16_t* text, std::int32_t capacity)
            {
                if (!Made(context, Window::MainWindow().value, "Window.GetClipboard"))
                {
                    return 0;
                }
                const std::u16string utf16 = Detail::Utf8ToUtf16(Window::ClipboardText(context.GetApp()));
                const std::size_t room = text == nullptr || capacity < 0 ? 0 : static_cast<std::size_t>(capacity);
                std::copy_n(utf16.begin(), std::min(room, utf16.size()), text);
                return static_cast<std::int32_t>(utf16.size());
            }

            bool SetClipboard(const CallContext& context, std::string_view text)
            {
                return Made(context, Window::MainWindow().value, "Window.SetClipboard") &&
                       Window::SetClipboardText(context.GetApp(), text);
            }

            bool RequestClose(const CallContext& context, std::uint32_t window)
            {
                return Made(context, window, "Window.RequestClose") &&
                       Window::RequestClose(context.GetApp(), WindowId{window});
            }
        } // namespace Native
    } // namespace

    Fenrir::ScriptingExtension Window::ScriptingExtension()
    {
        const std::string prefix = "Fenrir.Window.WindowNative.";
        Fenrir::ScriptingExtension extension;
        extension.name = "FenrirWindow";
        extension.sharedAssemblies = {"Fenrir.Window.dll"};
        extension.functions = {
            NativeFunction::Make<&Native::Exists>(prefix + "Exists"),
            NativeFunction::Make<&Native::Size>(prefix + "Size"),
            NativeFunction::Make<&Native::PixelSize>(prefix + "PixelSize"),
            NativeFunction::Make<&Native::ContentScale>(prefix + "ContentScale"),
            NativeFunction::Make<&Native::IsMinimised>(prefix + "IsMinimised"),
            NativeFunction::Make<&Native::IsFocused>(prefix + "IsFocused"),
            NativeFunction::Make<&Native::GetCursorMode>(prefix + "GetCursorMode"),
            NativeFunction::Make<&Native::SetCursorMode>(prefix + "SetCursorMode"),
            NativeFunction::Make<&Native::SetCursorShape>(prefix + "SetCursorShape"),
            NativeFunction::Make<&Native::SetTitle>(prefix + "SetTitle"),
            NativeFunction::Make<&Native::GetClipboard>(prefix + "GetClipboard"),
            NativeFunction::Make<&Native::SetClipboard>(prefix + "SetClipboard"),
            NativeFunction::Make<&Native::RequestClose>(prefix + "RequestClose"),
        };
        extension.events = {
            MakeEvent<WindowResizedEvent>("Fenrir.Window.WindowResizedEvent"),
            MakeEvent<WindowCloseRequestedEvent>("Fenrir.Window.WindowCloseRequestedEvent"),
        };
        return extension;
    }
} // namespace Fenrir
