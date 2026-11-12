#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Cascade.hpp"
#include "FenrirPainter/StopTexture.hpp"
#include "FenrirUI/Element.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "InputRouting.hpp"
#include "LayoutBridge.hpp"

namespace Fenrir
{
    class App;
    class UIRoots;
} // namespace Fenrir

namespace Fenrir::Detail
{
    /** @brief One slot of a root's dense tree: an element while `alive`, free for reuse otherwise. */
    struct ElementSlot
    {
        std::uint32_t version = 0;
        bool alive = false;
        std::unique_ptr<Element> element;
        std::string tag;
        std::string idString;
        std::vector<std::string> classes;
        std::uint16_t pseudoClasses = 0;
        StyleDeclarations inlineStyle;
        ResolvedStyle resolved;
        /** @brief Its custom properties as of its last resolve; null for none. */
        std::shared_ptr<const CustomProperties> customProperties;
        ElementId parent;
        std::vector<ElementId> children;
        LayoutNode* node = nullptr;
        ElementBoxes boxes;
        /** @brief Its style needs resolving. */
        bool styleDirty = true;
        /** @brief Something under it needs its style resolved. */
        bool descendantStyleDirty = false;
        /** @brief Whether a click, Tab or UIRoot::Focus can give it the focus. */
        bool focusable = false;
        /** @brief Its listeners, in the order added. */
        std::vector<ListenerEntry> listeners;
    };

    /** @brief A root's tree in dense slots, its Yoga nodes, its target and what its frames keep. */
    struct UIRootState
    {
        UIRootState(App& app, UIRoots& roots, UIRootConfig config) : app(app), roots(roots), config(std::move(config))
        {
        }

        App& app;
        UIRoots& roots;
        UIRootConfig config;
        /** @brief Where it draws: the configured target, or its window's overlay layer. */
        std::shared_ptr<UITarget> target;
        std::vector<ElementSlot> slots;
        std::vector<std::uint32_t> freeSlots;
        ElementId rootElement;
        LayoutTree layout;
        UITargetSize size;
        float scale = 1.0f;
        /** @brief Target pixels per UI unit, which the rectangles were last snapped at; zero before any were. */
        float snappedAt = 0.0f;
        UIRootStats stats;
        /** @brief The rows of its gradients with more than two stops, made on its first frame. */
        std::optional<StopTexture> stops;
        bool stopsUsed = false;
        /** @brief Its stylesheets, in the order added. */
        std::vector<std::unique_ptr<StylesheetEntry>> sheets;
        std::uint32_t nextSheet = 1;
        StyleIndex index;
        /** @brief Scratch for matching, kept to reuse their storage. */
        std::vector<std::uint32_t> candidates;
        std::vector<IndexedSelector> matched;
        /** @brief Its input router, hover, press and focus. */
        UIRootInput input;

        /** @brief The slot `id` names, or null when it is stale. */
        [[nodiscard]] ElementSlot* Find(ElementId id) noexcept
        {
            if (id.index >= slots.size() || !slots[id.index].alive || slots[id.index].version != id.version)
            {
                return nullptr;
            }
            return &slots[id.index];
        }

        /** @copydoc Find */
        [[nodiscard]] const ElementSlot* Find(ElementId id) const noexcept
        {
            return const_cast<UIRootState*>(this)->Find(id);
        }
    };
} // namespace Fenrir::Detail
