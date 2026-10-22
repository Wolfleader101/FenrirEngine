#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Input.hpp"

namespace InputTests
{
    // A short description of an input, for journals the tests compare with literals: "key A down", "key A repeat",
    // "key A up", "key A cancelled", "button Left down", "motion", "scroll", "text a", "focus lost" and so on.
    inline std::string Describe(const Fenrir::RoutedInput& input)
    {
        return std::visit(
            [](const auto& event) -> std::string {
                using Event = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<Event, Fenrir::KeyEvent>)
                {
                    // Only the letters the tests use.
                    const char letter = static_cast<char>('A' + (static_cast<int>(event.key) - 1));
                    const std::string what = event.cancelled ? "cancelled"
                                             : event.repeat  ? "repeat"
                                             : event.pressed ? "down"
                                                             : "up";
                    return std::string("key ") + letter + " " + what;
                }
                else if constexpr (std::is_same_v<Event, Fenrir::MouseButtonEvent>)
                {
                    const std::string what = event.cancelled ? "cancelled" : event.pressed ? "down" : "up";
                    return std::string("button ") + (event.button == Fenrir::MouseButton::Left ? "Left" : "Right") +
                           " " + what;
                }
                else if constexpr (std::is_same_v<Event, Fenrir::MouseMotionEvent>)
                {
                    return "motion";
                }
                else if constexpr (std::is_same_v<Event, Fenrir::MouseScrollEvent>)
                {
                    return "scroll";
                }
                else if constexpr (std::is_same_v<Event, Fenrir::TextEvent>)
                {
                    return std::string("text ") + static_cast<char>(event.codepoint);
                }
                else if constexpr (std::is_same_v<Event, Fenrir::WindowFocusEvent>)
                {
                    return event.focused ? "focus gained" : "focus lost";
                }
                else if constexpr (std::is_same_v<Event, Fenrir::GamepadButtonEvent>)
                {
                    return std::string("pad button ") + (event.pressed ? "down" : "up");
                }
                else if constexpr (std::is_same_v<Event, Fenrir::GamepadAxisEvent>)
                {
                    return "pad axis";
                }
                else
                {
                    return event.connected ? "pad connected" : "pad disconnected";
                }
            },
            input);
    }

    // A journal shared by several routers: "<router>: begin" and "<router>: <input>" in the order the calls came.
    using Journal = std::shared_ptr<std::vector<std::string>>;

    inline Journal NewJournal()
    {
        return std::make_shared<std::vector<std::string>>();
    }

    // An input router that writes each call to a journal and answers as `decide` says (Passed unless set).
    class TestRouter : public Fenrir::InputRouter
    {
      public:
        TestRouter(std::string name, Journal journal) : m_name(std::move(name)), m_journal(std::move(journal))
        {
        }

        std::function<Fenrir::RouteResult(const Fenrir::RoutedInput&)> decide;
        std::function<void(Fenrir::App&)> onBegin;
        std::function<void(Fenrir::App&, const Fenrir::RoutedInput&)> onRoute;
        int begins = 0;

        void BeginDispatch(Fenrir::App& app) override
        {
            ++begins;
            m_journal->push_back(m_name + ": begin");
            if (onBegin)
            {
                onBegin(app);
            }
        }

        Fenrir::RouteResult Route(Fenrir::App& app, const Fenrir::RoutedInput& input) override
        {
            m_journal->push_back(m_name + ": " + Describe(input));
            if (onRoute)
            {
                onRoute(app, input);
            }
            return decide ? decide(input) : Fenrir::RouteResult::Passed;
        }

      private:
        std::string m_name;
        Journal m_journal;
    };

    inline Fenrir::RouteResult ConsumeAll(const Fenrir::RoutedInput&)
    {
        return Fenrir::RouteResult::Consumed;
    }

    // Consumes presses of keys and mouse buttons (not repeats), passing everything else.
    inline Fenrir::RouteResult ConsumePresses(const Fenrir::RoutedInput& input)
    {
        if (const auto* key = std::get_if<Fenrir::KeyEvent>(&input); key && key->pressed && !key->repeat)
        {
            return Fenrir::RouteResult::Consumed;
        }
        if (const auto* button = std::get_if<Fenrir::MouseButtonEvent>(&input); button && button->pressed)
        {
            return Fenrir::RouteResult::Consumed;
        }
        return Fenrir::RouteResult::Passed;
    }

    // Every event `reader` has not read yet, described.
    template <typename TEvent>
    std::vector<std::string> ReadDescribed(Fenrir::EventReader<TEvent>& reader, const Fenrir::App& app)
    {
        std::vector<std::string> read;
        for (const TEvent& event : reader.Read(app))
        {
            read.push_back(Describe(Fenrir::RoutedInput(event)));
        }
        return read;
    }

    // The journal's entries that are not BeginDispatch calls.
    inline std::vector<std::string> Routed(const Journal& journal)
    {
        std::vector<std::string> routed;
        for (const std::string& entry : *journal)
        {
            if (!entry.ends_with(": begin"))
            {
                routed.push_back(entry);
            }
        }
        return routed;
    }

    using Lines = std::vector<std::string>;
} // namespace InputTests
