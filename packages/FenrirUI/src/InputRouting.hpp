#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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
        std::uint32_t nextListener = 1;
    };

    /** @brief Registers the root's router with FenrirInput when the app has input and it is not registered yet. */
    void StartInput(UIRoot& root, UIRootState& state);
    /** @brief Removes the root's router from FenrirInput; it sees no more input. */
    void StopInput(UIRootState& state);
    /**
     * @brief After the tree changed: forgets hovered, pressed and focused elements that were removed and puts :hover,
     * :active and :focus-within back on the elements now above them, then publishes the claims.
     */
    void RefreshInput(UIRoot& root, UIRootState& state);

    /** @copydoc Fenrir::UIRoot::HitTest */
    [[nodiscard]] ElementId HitTest(const UIRootState& state, Math::Vec2 position);
    /** @copydoc Fenrir::UIRoot::Focus */
    bool Focus(UIRoot& root, UIRootState& state, ElementId element);
    /** @copydoc Fenrir::UIRoot::Blur */
    void Blur(UIRoot& root, UIRootState& state);
    /** @copydoc Fenrir::UIRoot::SendEvent */
    void SendUIEvent(UIRootState& state, UIEvent event);
} // namespace Fenrir::Detail
