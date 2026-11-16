#pragma once

#include <cstdint>
#include <functional>

#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirUI/ElementId.hpp"

namespace Fenrir
{
    /** @brief Names a UI root; UIRoots gives each root its own when it is made. Zero names none. */
    struct UIRootId
    {
        std::uint32_t value = 0;

        friend constexpr bool operator==(UIRootId, UIRootId) noexcept = default;
    };

    /** @brief What happened to a UIEvent's target. */
    enum class UIEventKind : std::uint8_t
    {
        /** @brief The pointer came over the target: it is now the topmost element under the pointer that takes it. */
        PointerEnter,
        /** @brief The pointer left the target. */
        PointerLeave,
        /** @brief The pointer moved over the target. */
        PointerMove,
        /** @brief A mouse button went down over the target. */
        PointerPress,
        /** @brief A mouse button pressed over the target went up, wherever the pointer is now. */
        PointerRelease,
        /** @brief A button was pressed and released over the target. */
        Click,
        /** @brief The second click of a button on the target in quick succession, sent after its Click. */
        DoubleClick,
        /** @brief The wheel turned over the target. */
        Scroll,
        /** @brief A key went down, or repeated, while the target had the focus. */
        KeyPress,
        /** @brief A key went up while the target had the focus. */
        KeyRelease,
        /** @brief A codepoint was typed while the target had the focus. */
        TextInput,
        /** @brief The target took the focus. */
        Focus,
        /** @brief The target lost the focus. */
        Blur,
        /** @brief A control's value changed. Sent by the controls. */
        Change,
        /** @brief A control was submitted, such as Enter in a text entry. Sent by the controls. */
        Submit,
        /** @brief A markup document was loaded again into the target's place. Sent by hot reload. */
        DocumentReloaded,
        /** @brief B on a gamepad (its East button) was pressed while the target had the focus: back out of it. */
        Cancel,
    };

    /**
     * @brief A UI event (ADR-0065): an engine event (ADR-0012) a UI root sends once for each thing that happens to one
     * of its elements, read through `EventReader<UIEvent>`. It is sent at its target only; listeners on the target's
     * ancestors see it as it bubbles, the queue does not. Plain data.
     */
    struct UIEvent
    {
        /** @brief The root whose element it happened to. */
        UIRootId root;
        /** @brief The element it happened to. */
        ElementId target;
        UIEventKind kind = UIEventKind::PointerMove;
        /** @brief Where the pointer is, in the root's UI units from its top-left, for every kind. */
        Math::Vec2 position{0.0f, 0.0f};
        /** @brief The mouse button, for a press, release, click or double click. */
        MouseButton button = MouseButton::Left;
        /** @brief The key, for a key press or release. */
        Key key = Key::Unknown;
        /**
         * @brief The vertical wheel notches for a scroll (positive away from the user), 1 for a key press that is a
         * held key's repeat, and a control's value for a change.
         */
        float value = 0.0f;
        /**
         * @brief The modifiers (KeyModifiers' bits) for a key press or release, the codepoint for text input, and the
         * click count for a click (1) or double click (2).
         */
        std::int32_t integer = 0;
    };

    /** @brief What a UI listener is called with: the event, the element it has bubbled to, and a way to stop it. */
    class UIListenerContext
    {
      public:
        UIListenerContext(const UIEvent& event, ElementId current) noexcept : m_event(event), m_current(current)
        {
        }

        /** @brief The event, as the queue has it. */
        [[nodiscard]] const UIEvent& GetEvent() const noexcept
        {
            return m_event;
        }

        /** @brief The element whose listener is running: the target, or one of its ancestors. */
        [[nodiscard]] ElementId GetCurrent() const noexcept
        {
            return m_current;
        }

        /**
         * @brief Keeps the event from bubbling to the current element's ancestors. The current element's other
         * listeners still run, as the web's `stopPropagation` does.
         */
        void StopPropagation() noexcept
        {
            m_stopped = true;
        }

        /** @brief Whether a listener stopped it. */
        [[nodiscard]] bool IsStopped() const noexcept
        {
            return m_stopped;
        }

      private:
        const UIEvent& m_event;
        ElementId m_current;
        bool m_stopped = false;
    };

    /**
     * @brief A UI listener: C++ code attached to an element for one kind of UIEvent, called when that kind happens to
     * the element or bubbles up to it from an element under it (ADR-0065). It may change the root's tree.
     */
    using UIListener = std::function<void(UIListenerContext& context)>;

    /** @brief Names one listener of one element; UIRoot::AddListener gives it. Default constructed, it names none. */
    struct UIListenerId
    {
        ElementId element;
        std::uint32_t serial = 0;

        friend constexpr bool operator==(UIListenerId, UIListenerId) noexcept = default;
    };
} // namespace Fenrir
