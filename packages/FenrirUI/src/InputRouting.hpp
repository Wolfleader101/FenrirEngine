#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "FenrirInput/InputRouter.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/UIEvent.hpp"

namespace Fenrir
{
    class UIRoot;
} // namespace Fenrir

// A UI root's input (ADR-0065, ADR-0027): its input router, hover, press and focus, and sending UIEvents through the
// queue and the listeners.
namespace Fenrir::Detail
{
    struct UIRootState;
    class RootRouter;

    /** @brief One listener on an element, shared so a dispatch can hold it while listeners change the list. */
    struct ListenerEntry
    {
        std::uint32_t serial = 0;
        UIEventKind kind = UIEventKind::Click;
        std::shared_ptr<UIListener> listener;
    };

    /** @brief A root's input state: what its router tracks between inputs and frames. */
    struct UIRootInput
    {
        UIRootId id;
        std::shared_ptr<RootRouter> router;
        InputRouterId routerId;
        /** @brief Where the pointer last was in the root's window, in UI units; none before it moved there. */
        std::optional<Math::Vec2> pointer;
        ElementId hovered;
        /** @brief The element each mouse button was pressed on, while held. */
        std::array<ElementId, static_cast<std::size_t>(MouseButton::Count)> pressed{};
        /** @brief UIRoot::TakePress moved the held buttons: their releases send no click. */
        bool pressTaken = false;
        ElementId focused;
        /** @brief The elements carrying :hover, :active and :focus-within, to clear when they move. */
        std::vector<ElementId> hoverChain;
        std::vector<ElementId> activeChain;
        std::vector<ElementId> focusChain;
        /** @brief The claims last published for the root's window. */
        WindowInputClaims published;
        /** @brief Seconds of frames the router has seen, for double clicks. */
        double clock = 0.0;
        /** @brief The last click, for the double click a second one makes. */
        struct
        {
            ElementId element;
            MouseButton button = MouseButton::Left;
            double time = 0.0;
            Math::Vec2 position{0.0f, 0.0f};
            bool armed = false;
        } lastClick;
        /** @brief The element each held key's press went to, so its release goes there even if the focus moved. */
        std::vector<std::pair<Scancode, ElementId>> keyPresses;
        std::uint32_t nextListener = 1;
        /** @brief The gamepad claim last published. */
        bool gamepadPublished = false;
        /** @brief The left stick as it last moved, from whichever pad moved it: x right, y down. */
        Math::Vec2 stick{0.0f, 0.0f};
        /** @brief The direction the stick leans past the navigation dead zone, as its D-pad button; none inside it. */
        std::optional<GamepadButton> stickDirection;
        /** @brief The direction held that moved the focus, D-pad or stick, while it repeats. */
        struct
        {
            std::optional<GamepadButton> direction;
            GamepadId pad;
            bool fromStick = false;
            /** @brief The clock reading at which it moves the focus again. */
            double next = 0.0;
        } held;
        /** @brief Whether the virtual cursor is on, its element while it is, and whether A holds its press. */
        bool virtualCursor = false;
        ElementId cursorElement;
        bool cursorPressed = false;
    };

    /** @brief Registers the root's router with FenrirInput when the app has input and it is not registered yet. */
    void StartInput(UIRoot& root, UIRootState& state);
    /**
     * @brief Removes the root's router from FenrirInput: it sees no more input. With `detach`, for a root being freed,
     * it is cut from the state too; without, a route under way can finish on the state.
     */
    void StopInput(UIRootState& state, bool detach);
    /**
     * @brief After the tree changed: forgets hovered, pressed and focused elements that were removed and puts :hover,
     * :active and :focus-within back on the elements now above them, then publishes the claims.
     */
    void RefreshInput(UIRoot& root, UIRootState& state);

    /**
     * @brief Each frame with a size, before styles resolve: while the virtual cursor is on, makes its element when it
     * has none and starts the pointer at the root's centre (`uiSize` halved) when it has not been anywhere.
     */
    void FrameInput(UIRoot& root, UIRootState& state, Math::Vec2 uiSize);
    /** @copydoc Fenrir::UIRoot::SetVirtualCursor */
    void SetVirtualCursor(UIRoot& root, UIRootState& state, bool on);

    /** @copydoc Fenrir::UIRoot::HitTest */
    [[nodiscard]] ElementId HitTest(const UIRootState& state, Math::Vec2 position);
    /** @copydoc Fenrir::UIRoot::Focus */
    bool Focus(UIRoot& root, UIRootState& state, ElementId element);
    /** @copydoc Fenrir::UIRoot::Blur */
    void Blur(UIRoot& root, UIRootState& state);
    /** @copydoc Fenrir::UIRoot::TakePress */
    bool TakePress(UIRoot& root, UIRootState& state, ElementId element);
    /** @copydoc Fenrir::UIRoot::SendEvent */
    void SendUIEvent(UIRootState& state, UIEvent event);
} // namespace Fenrir::Detail
