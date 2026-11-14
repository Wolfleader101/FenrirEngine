#include "FenrirUI/UIRoot.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "BoxPainting.hpp"
#include "Cascade.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirWindow/Window.hpp"
#include "Rects.hpp"
#include "Scrolling.hpp"
#include "StyleAnimation.hpp"
#include "StyleFiles.hpp"
#include "StyleValues.hpp"
#include "UIRootState.hpp"

namespace Fenrir
{
    namespace
    {
        /** @brief A root's default target: its window's overlay layer, at the window's size. */
        class WindowOverlayTarget final : public UITarget
        {
          public:
            WindowOverlayTarget(WindowId window, std::int32_t layer) : m_window(window), m_layer(layer)
            {
            }

            UITargetSize GetSize(App& app) override
            {
                // Window's functions give zero sizes for a window that does not exist, or with no Window installed.
                if (Window::IsMinimised(app, m_window))
                {
                    return {};
                }
                const WindowSize logical = Window::LogicalSize(app, m_window);
                return {{static_cast<float>(logical.width), static_cast<float>(logical.height)},
                        Window::ContentScale(app, m_window)};
            }

            void Submit(App& app, DrawList2D list) override
            {
                if (Overlay* overlay = app.TryGetResource<Overlay>())
                {
                    overlay->Submit(m_window, m_layer, std::move(list));
                }
            }

          private:
            WindowId m_window;
            std::int32_t m_layer;
        };

        float Snap(float value, float pixelsPerUnit)
        {
            if (pixelsPerUnit <= 0.0f)
            {
                return value;
            }
            // Half a pixel goes up, the same way for every edge, so neighbours that share an edge share its pixel.
            return std::floor(value * pixelsPerUnit + 0.5f) / pixelsPerUnit;
        }

        Rect2D SnappedRect(float left, float top, float right, float bottom, float pixelsPerUnit)
        {
            const float x0 = Snap(left, pixelsPerUnit);
            const float y0 = Snap(top, pixelsPerUnit);
            const float x1 = std::max(x0, Snap(right, pixelsPerUnit));
            const float y1 = std::max(y0, Snap(bottom, pixelsPerUnit));
            return {x0, y0, x1 - x0, y1 - y0};
        }

        /** @brief Whether `a` and `b` differ in a property children inherit. */
        bool InheritedDiffer(const ResolvedStyle& a, const ResolvedStyle& b)
        {
            for (std::size_t i = 0; i < StylePropertyCount; ++i)
            {
                const auto property = static_cast<StyleProperty>(i);
                if (Detail::IsInherited(property) && !Detail::SameProperty(property, a, b))
                {
                    return true;
                }
            }
            return false;
        }

        /** @brief Gives the element's text the style it is shown with, measuring it again when its size may change. */
        void UpdateTextStyle(Detail::UIRootState& state, Detail::ElementSlot& slot)
        {
            if (slot.text != nullptr && slot.text->SetStyle(slot.resolved, state.textPixelsPerUnit))
            {
                state.layout.InvalidateMeasure(slot.node);
            }
        }

        /**
         * @brief Resolves the styles under slot `index` that are dirty or inherit a changed value, starting the
         * transitions and animations their changes call for, and writes the shown style of those and of the elements
         * whose motion moved.
         */
        void ResolveStyles(Detail::UIRootState& state, std::uint32_t index, const Detail::ElementSlot* parent,
                           bool parentInheritedChanged)
        {
            const bool resolve = state.slots[index].styleDirty || parentInheritedChanged;
            bool inheritedChanged = false;
            bool customChanged = false;
            if (resolve)
            {
                ResolvedStyle style;
                std::shared_ptr<const Detail::CustomProperties> custom;
                if (parent != nullptr)
                {
                    Detail::Inherit(parent->resolved, style);
                    custom = parent->customProperties;
                }
                Detail::Cascade(state, index, style, custom);
                Detail::ElementSlot& slot = state.slots[index];
                customChanged =
                    custom != slot.customProperties &&
                    (custom == nullptr || slot.customProperties == nullptr || *custom != *slot.customProperties);
                if (customChanged)
                {
                    slot.customProperties = std::move(custom);
                }
                if (slot.hasCascaded)
                {
                    Detail::StartTransitions(slot.motion, slot.cascaded, style);
                }
                Detail::ReconcileAnimations(slot.motion, style, state.index.keyframes);
                // Its pseudo-elements are parts of it: they inherit from it and match its pseudo-classes.
                const auto resolvePart = [&](ResolvedStyle& part, PseudoElement pseudoElement) {
                    Detail::Inherit(style, part);
                    std::shared_ptr<const Detail::CustomProperties> partCustom = slot.customProperties;
                    Detail::Cascade(state, index, part, partCustom, pseudoElement);
                };
                if (dynamic_cast<const TextEntry*>(slot.element.get()) != nullptr)
                {
                    auto parts = std::make_unique<Detail::TextEntryStyles>();
                    resolvePart(parts->selection, PseudoElement::Selection);
                    resolvePart(parts->placeholder, PseudoElement::Placeholder);
                    slot.textEntryStyles = std::move(parts);
                }
                if (style.overflow == Overflow::Scroll)
                {
                    auto parts = std::make_unique<Detail::ScrollbarStyles>();
                    resolvePart(parts->track, PseudoElement::Scrollbar);
                    resolvePart(parts->thumb, PseudoElement::ScrollbarThumb);
                    slot.scrollbarStyles = std::move(parts);
                }
                else
                {
                    slot.scrollbarStyles.reset();
                    slot.scrollOffset = Math::Vec2(0.0f);
                    slot.scrollRange = Math::Vec2(0.0f);
                }
                slot.cascaded = std::move(style);
                slot.hasCascaded = true;
                slot.styleDirty = false;
                ++state.stats.stylesResolved;
            }
            Detail::ElementSlot& slot = state.slots[index];
            if (resolve || slot.motionDirty)
            {
                ResolvedStyle shown = slot.cascaded;
                Detail::ApplyMotion(slot.motion, shown);
                inheritedChanged = InheritedDiffer(shown, slot.resolved) || customChanged;
                slot.resolved = std::move(shown);
                state.layout.ApplyStyle(slot.node, slot.resolved);
                UpdateTextStyle(state, slot);
                slot.motionDirty = false;
            }
            if (inheritedChanged || state.slots[index].descendantStyleDirty)
            {
                for (const ElementId child : state.slots[index].children)
                {
                    ResolveStyles(state, child.index, &state.slots[index], inheritedChanged);
                }
            }
            state.slots[index].descendantStyleDirty = false;
        }

        /**
         * @brief Reads back the rectangles under slot `index`, whose parent's unsnapped border box is at `parentLeft`,
         * `parentTop`, snapping each edge at `pixelsPerUnit` (none at 0).
         */
        void ReadBoxes(Detail::UIRootState& state, std::uint32_t index, float parentLeft, float parentTop,
                       const Rect2D& clip, float pixelsPerUnit)
        {
            Detail::ElementSlot& slot = state.slots[index];
            const Detail::LayoutResult result = state.layout.Result(slot.node);
            const float left = parentLeft + result.rect.x;
            const float top = parentTop + result.rect.y;
            const float right = left + result.rect.width;
            const float bottom = top + result.rect.height;
            const Sides& border = result.border;
            const Sides& padding = result.padding;
            slot.boxes.border = SnappedRect(left, top, right, bottom, pixelsPerUnit);
            slot.boxes.padding = SnappedRect(left + border.left, top + border.top, right - border.right,
                                             bottom - border.bottom, pixelsPerUnit);
            slot.boxes.content = SnappedRect(left + border.left + padding.left, top + border.top + padding.top,
                                             right - border.right - padding.right,
                                             bottom - border.bottom - padding.bottom, pixelsPerUnit);
            slot.boxes.clip = clip;
            if (slot.text != nullptr)
            {
                // The unsnapped width, which the paragraph was measured to fit.
                slot.text->SetContentWidth(result.rect.width - border.left - border.right - padding.left -
                                           padding.right);
            }
            const Rect2D childClip =
                slot.resolved.overflow != Overflow::Visible ? Detail::Intersect(clip, slot.boxes.padding) : clip;
            Math::Vec2 offset(0.0f);
            if (slot.resolved.overflow == Overflow::Scroll)
            {
                // Its content reaches as far as its children's border boxes, and its padding past them, from its
                // padding box's top-left.
                Math::Vec2 reach(0.0f);
                for (const ElementId child : slot.children)
                {
                    const Detail::ElementSlot& under = state.slots[child.index];
                    if (under.resolved.display == Display::None)
                    {
                        continue;
                    }
                    const Rect2D rect = state.layout.Result(under.node).rect;
                    reach = glm::max(reach, Math::Vec2(rect.x + rect.width - border.left + padding.right,
                                                       rect.y + rect.height - border.top + padding.bottom));
                }
                const Math::Vec2 shown(result.rect.width - border.left - border.right,
                                       result.rect.height - border.top - border.bottom);
                slot.scrollRange = glm::max(Math::Vec2(0.0f), reach - shown);
                slot.scrollOffset = glm::clamp(slot.scrollOffset, Math::Vec2(0.0f), slot.scrollRange);
                offset = slot.scrollOffset;
            }
            for (const ElementId child : slot.children)
            {
                ReadBoxes(state, child.index, left - offset.x, top - offset.y, childClip, pixelsPerUnit);
            }
        }

        /** @brief Marks the element's style dirty, and the path above it. */
        void MarkStyleDirty(Detail::UIRootState& state, ElementId id)
        {
            Detail::ElementSlot* slot = state.Find(id);
            if (slot == nullptr)
            {
                return;
            }
            slot->styleDirty = true;
            for (Detail::ElementSlot* above = state.Find(slot->parent);
                 above != nullptr && !above->descendantStyleDirty; above = state.Find(above->parent))
            {
                above->descendantStyleDirty = true;
            }
        }

        /** @brief Marks the element and everything under it dirty, and the path above it. */
        void MarkSubtreeStyleDirty(Detail::UIRootState& state, ElementId id)
        {
            Detail::ElementSlot* slot = state.Find(id);
            if (slot == nullptr)
            {
                return;
            }
            MarkStyleDirty(state, id);
            const auto mark = [&state](const auto& self, Detail::ElementSlot& under) -> void {
                under.styleDirty = true;
                under.descendantStyleDirty = !under.children.empty();
                for (const ElementId child : under.children)
                {
                    self(self, state.slots[child.index]);
                }
            };
            mark(mark, *slot);
        }

        /** @brief Moves every element's transitions and animations on by `seconds`, marking those whose moved. */
        void AdvanceMotion(Detail::UIRootState& state, float seconds)
        {
            for (std::uint32_t index = 0; index < state.slots.size(); ++index)
            {
                Detail::ElementSlot& slot = state.slots[index];
                if (!slot.alive || !slot.motion.IsMoving() ||
                    !Detail::AdvanceMotion(slot.motion, slot.cascaded, seconds))
                {
                    continue;
                }
                slot.motionDirty = true;
                for (Detail::ElementSlot* above = state.Find(slot.parent);
                     above != nullptr && !above->descendantStyleDirty; above = state.Find(above->parent))
                {
                    above->descendantStyleDirty = true;
                }
            }
        }

        /** @brief Whether a transition runs on the element in slot `index` or under it. */
        bool HasTransitions(const Detail::UIRootState& state, std::uint32_t index)
        {
            const Detail::ElementSlot& slot = state.slots[index];
            return !slot.motion.transitions.empty() ||
                   std::any_of(slot.children.begin(), slot.children.end(),
                               [&](ElementId child) { return HasTransitions(state, child.index); });
        }

        /**
         * @brief Marks what a change to the element's classes, id or pseudo-classes can restyle: the element, and its
         * subtree when a compound before a combinator may match it, by its tag, id or classes or by `former`, a class
         * or id it has just lost.
         */
        void MarkMatchDirty(Detail::UIRootState& state, ElementId id, std::string_view former = {})
        {
            const Detail::ElementSlot* slot = state.Find(id);
            if (slot == nullptr)
            {
                return;
            }
            const Detail::StyleIndex& index = state.index;
            const auto named = [](const std::unordered_set<std::string>& names, std::string_view name) {
                return !name.empty() && names.contains(std::string(name));
            };
            const bool above = index.anyAbove || named(index.aboveTags, slot->tag) ||
                               named(index.aboveIds, slot->idString) || named(index.aboveIds, former) ||
                               named(index.aboveClasses, former) ||
                               std::any_of(slot->classes.begin(), slot->classes.end(),
                                           [&](const std::string& name) { return named(index.aboveClasses, name); });
            if (above)
            {
                MarkSubtreeStyleDirty(state, id);
            }
            else
            {
                MarkStyleDirty(state, id);
            }
        }

        /** @brief After `parent`'s children changed, marks those :first-child and :last-child may now match otherwise.
         */
        void MarkChildrenMatchDirty(Detail::UIRootState& state, ElementId parent)
        {
            const Detail::ElementSlot* slot = state.Find(parent);
            if (slot == nullptr || !state.index.hasSiblingStructure)
            {
                return;
            }
            for (const ElementId child : slot->children)
            {
                MarkMatchDirty(state, child);
            }
        }

        /** @brief Frees slot `index` and everything under it, their Yoga nodes with them. */
        void FreeSubtree(Detail::UIRootState& state, std::uint32_t index)
        {
            const std::vector<ElementId> children = state.slots[index].children;
            for (const ElementId child : children)
            {
                FreeSubtree(state, child.index);
            }
            Detail::ElementSlot& slot = state.slots[index];
            state.layout.DestroyNode(slot.node);
            const std::uint32_t version = slot.version;
            slot = Detail::ElementSlot{};
            slot.version = version;
            state.freeSlots.push_back(index);
        }

        void Detach(Detail::UIRootState& state, ElementId id)
        {
            Detail::ElementSlot& slot = *state.Find(id);
            Detail::ElementSlot& parent = *state.Find(slot.parent);
            parent.children.erase(std::find(parent.children.begin(), parent.children.end(), id));
            state.layout.RemoveChild(parent.node, slot.node);
            slot.parent = {};
        }

        void Attach(Detail::UIRootState& state, ElementId id, ElementId parentId, std::size_t index)
        {
            Detail::ElementSlot& parent = *state.Find(parentId);
            index = std::min(index, parent.children.size());
            parent.children.insert(parent.children.begin() + static_cast<std::ptrdiff_t>(index), id);
            Detail::ElementSlot& slot = *state.Find(id);
            state.layout.InsertChild(parent.node, slot.node, index);
            slot.parent = parentId;
        }
    } // namespace

    UIRoot::UIRoot(App& app, UIRoots& roots, UIRootConfig config, UIRootId id)
        : m_state(std::make_unique<Detail::UIRootState>(app, roots, std::move(config)))
    {
        m_state->input.id = id;
        m_state->input.cursor.on = m_state->config.virtualCursor;
        m_state->hotReload.on = m_state->config.hotReload;
        m_state->target = m_state->config.target != nullptr
                              ? m_state->config.target
                              : std::make_shared<WindowOverlayTarget>(m_state->config.window, m_state->config.layer);
        if (!m_state->config.blendSpace)
        {
            m_state->config.blendSpace = m_state->target->GetDefaultBlendSpace();
        }
        Detail::ElementSlot& slot = m_state->slots.emplace_back();
        slot.version = 1;
        slot.alive = true;
        slot.tag = "root";
        slot.element = std::make_unique<Element>();
        m_state->rootElement = {0, 1};
        slot.element->m_id = m_state->rootElement;
        slot.element->m_root = this;
        slot.node = m_state->layout.CreateNode(slot.element.get());
        if (m_state->config.defaultStylesheet)
        {
            m_state->defaultSheet = std::make_unique<Detail::StylesheetEntry>();
            m_state->defaultSheet->sheet = Stylesheet::Parse(DefaultStylesheet(), "FenrirUI default stylesheet");
            Detail::CompileStylesheet(*m_state, *m_state->defaultSheet);
            Detail::RebuildStyleIndex(*m_state);
        }
        Detail::StartInput(*this, *m_state);
    }

    UIRoot::~UIRoot()
    {
        Detail::StopInput(*m_state, true);
        // Every Yoga node is freed before the tree's configuration is.
        FreeSubtree(*m_state, m_state->rootElement.index);
    }

    const UIRootConfig& UIRoot::GetConfig() const noexcept
    {
        return m_state->config;
    }

    void UIRoot::Retire()
    {
        Detail::StopInput(*m_state, false);
    }

    UIRootId UIRoot::GetId() const noexcept
    {
        return m_state->input.id;
    }

    float UIRoot::GetScale() const noexcept
    {
        return m_state->scale;
    }

    UITargetSize UIRoot::GetTargetSize() const noexcept
    {
        return m_state->size;
    }

    const UIRootStats& UIRoot::GetStats() const noexcept
    {
        return m_state->stats;
    }

    App& UIRoot::GetApp() const noexcept
    {
        return m_state->app;
    }

    ElementId UIRoot::GetRootElement() const noexcept
    {
        return m_state->rootElement;
    }

    ElementId UIRoot::Create(ElementId parent, std::string_view tag, std::size_t index)
    {
        if (m_state->Find(parent) == nullptr)
        {
            return {};
        }
        std::uint32_t slotIndex = 0;
        if (!m_state->freeSlots.empty())
        {
            slotIndex = m_state->freeSlots.back();
            m_state->freeSlots.pop_back();
        }
        else
        {
            slotIndex = static_cast<std::uint32_t>(m_state->slots.size());
            m_state->slots.emplace_back();
        }
        Detail::ElementSlot& slot = m_state->slots[slotIndex];
        ++slot.version;
        slot.alive = true;
        slot.tag = std::string(tag);
        slot.element = m_state->roots.MakeElement(tag);
        const ElementId id{slotIndex, slot.version};
        slot.element->m_id = id;
        slot.element->m_root = this;
        slot.node = m_state->layout.CreateNode(slot.element.get());
        slot.pseudoClasses = static_cast<std::uint16_t>(PseudoClass::Intro);
        m_state->introducing.push_back(id);
        Attach(*m_state, id, parent, index);
        MarkStyleDirty(*m_state, id);
        MarkChildrenMatchDirty(*m_state, parent);
        // Last, as it may make children of its own, which can move the slots.
        m_state->Find(id)->element->OnCreate();
        return id;
    }

    bool UIRoot::Remove(ElementId element, RemoveMode mode)
    {
        Detail::ElementSlot* slot = m_state->Find(element);
        if (element == m_state->rootElement || slot == nullptr)
        {
            return false;
        }
        if (mode == RemoveMode::Outro)
        {
            if (!slot->removing)
            {
                slot->removing = true;
                m_state->removing.push_back(element);
                SetPseudoClass(element, PseudoClass::Outro, true);
            }
            return true;
        }
        const ElementId parent = GetParent(element);
        Detach(*m_state, element);
        FreeSubtree(*m_state, element.index);
        MarkChildrenMatchDirty(*m_state, parent);
        Detail::RefreshInput(*this, *m_state);
        return true;
    }

    bool UIRoot::Reparent(ElementId element, ElementId newParent, std::size_t index)
    {
        if (element == m_state->rootElement || m_state->Find(element) == nullptr || m_state->Find(newParent) == nullptr)
        {
            return false;
        }
        for (ElementId above = newParent; above.IsValid(); above = GetParent(above))
        {
            if (above == element)
            {
                return false;
            }
        }
        const ElementId oldParent = GetParent(element);
        Detach(*m_state, element);
        Attach(*m_state, element, newParent, index);
        // What it inherits, and what its ancestors let selectors match, may differ under its new parent.
        if (m_state->index.hasCombinators)
        {
            MarkSubtreeStyleDirty(*m_state, element);
        }
        else
        {
            MarkStyleDirty(*m_state, element);
        }
        MarkChildrenMatchDirty(*m_state, oldParent);
        MarkChildrenMatchDirty(*m_state, newParent);
        // What carries :hover, :active and :focus-within above it has changed.
        Detail::RefreshInput(*this, *m_state);
        return true;
    }

    bool UIRoot::Reorder(ElementId element, std::size_t index)
    {
        const ElementId parent = GetParent(element);
        if (!parent.IsValid())
        {
            return false;
        }
        Detach(*m_state, element);
        Attach(*m_state, element, parent, index);
        MarkChildrenMatchDirty(*m_state, parent);
        return true;
    }

    bool UIRoot::IsAlive(ElementId element) const noexcept
    {
        return m_state->Find(element) != nullptr;
    }

    ElementId UIRoot::GetParent(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? slot->parent : ElementId{};
    }

    std::span<const ElementId> UIRoot::GetChildren(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? std::span<const ElementId>(slot->children) : std::span<const ElementId>{};
    }

    Element* UIRoot::GetElement(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? slot->element.get() : nullptr;
    }

    std::string_view UIRoot::GetTag(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? std::string_view(slot->tag) : std::string_view{};
    }

    std::string_view UIRoot::GetIdString(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? std::string_view(slot->idString) : std::string_view{};
    }

    void UIRoot::SetIdString(ElementId element, std::string_view id)
    {
        if (Detail::ElementSlot* slot = m_state->Find(element))
        {
            const std::string former = std::exchange(slot->idString, std::string(id));
            MarkMatchDirty(*m_state, element, former);
        }
    }

    ElementId UIRoot::FindById(std::string_view id) const
    {
        ElementId found;
        const auto search = [&](const auto& self, ElementId element) -> bool {
            const Detail::ElementSlot& slot = *m_state->Find(element);
            if (slot.idString == id)
            {
                found = element;
                return true;
            }
            return std::any_of(slot.children.begin(), slot.children.end(),
                               [&](ElementId child) { return self(self, child); });
        };
        search(search, m_state->rootElement);
        return found;
    }

    std::vector<ElementId> UIRoot::FindByClass(std::string_view name) const
    {
        std::vector<ElementId> found;
        const auto search = [&](const auto& self, ElementId element) -> void {
            const Detail::ElementSlot& slot = *m_state->Find(element);
            if (std::find(slot.classes.begin(), slot.classes.end(), name) != slot.classes.end())
            {
                found.push_back(element);
            }
            for (const ElementId child : slot.children)
            {
                self(self, child);
            }
        };
        search(search, m_state->rootElement);
        return found;
    }

    std::span<const std::string> UIRoot::GetClasses(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? std::span<const std::string>(slot->classes) : std::span<const std::string>{};
    }

    void UIRoot::AddClass(ElementId element, std::string_view name)
    {
        Detail::ElementSlot* slot = m_state->Find(element);
        if (slot == nullptr || name.empty() || HasClass(element, name))
        {
            return;
        }
        slot->classes.emplace_back(name);
        MarkMatchDirty(*m_state, element);
    }

    void UIRoot::RemoveClass(ElementId element, std::string_view name)
    {
        Detail::ElementSlot* slot = m_state->Find(element);
        if (slot == nullptr)
        {
            return;
        }
        const auto it = std::find(slot->classes.begin(), slot->classes.end(), name);
        if (it != slot->classes.end())
        {
            const std::string former = std::move(*it);
            slot->classes.erase(it);
            MarkMatchDirty(*m_state, element, former);
        }
    }

    bool UIRoot::HasClass(ElementId element, std::string_view name) const
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr && std::find(slot->classes.begin(), slot->classes.end(), name) != slot->classes.end();
    }

    void UIRoot::SetPseudoClass(ElementId element, PseudoClass pseudoClass, bool on)
    {
        Detail::ElementSlot* slot = m_state->Find(element);
        if (slot == nullptr)
        {
            return;
        }
        const auto bit = static_cast<std::uint16_t>(pseudoClass);
        const auto flags = static_cast<std::uint16_t>(on ? (slot->pseudoClasses | bit) : (slot->pseudoClasses & ~bit));
        if (flags != slot->pseudoClasses)
        {
            slot->pseudoClasses = flags;
            MarkMatchDirty(*m_state, element);
        }
    }

    bool UIRoot::HasPseudoClass(ElementId element, PseudoClass pseudoClass) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr && (slot->pseudoClasses & static_cast<std::uint16_t>(pseudoClass)) != 0;
    }

    const StyleDeclarations* UIRoot::GetInlineStyle(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? &slot->inlineStyle : nullptr;
    }

    StyleDeclarations* UIRoot::EditInlineStyle(ElementId element)
    {
        Detail::ElementSlot* slot = m_state->Find(element);
        if (slot == nullptr)
        {
            return nullptr;
        }
        MarkStyleDirty(*m_state, element);
        return &slot->inlineStyle;
    }

    bool UIRoot::SetStyleProperty(ElementId element, std::string_view name, std::string_view value)
    {
        if (m_state->Find(element) == nullptr)
        {
            return false;
        }
        std::string error;
        if (!ApplyStyleProperty(element, name, value, error))
        {
            m_state->app.Logger()->Warn("FenrirUI: the inline style property '{}: {}' was not set: {}", name, value,
                                        error);
            return false;
        }
        return true;
    }

    bool UIRoot::ApplyStyleProperty(ElementId element, std::string_view name, std::string_view value,
                                    std::string& error)
    {
        Detail::ElementSlot* slot = m_state->Find(element);
        if (slot == nullptr)
        {
            return false;
        }
        Detail::StyleParseContext context = Detail::MakeParseContext(*m_state);
        const std::string lower = Detail::Lowered(name);
        bool set = false;
        if (Detail::ClassifyProperty(lower) == Detail::PropertyKind::NotYetStyled)
        {
            context.error = "FenrirUI does not style it yet";
        }
        else
        {
            set = Detail::ParseStyleDeclaration(lower, value, slot->inlineStyle, context);
        }
        if (!set)
        {
            error = std::move(context.error);
            return false;
        }
        MarkStyleDirty(*m_state, element);
        return true;
    }

    StylesheetId UIRoot::AddStylesheet(std::string_view text, std::string_view name)
    {
        return AddStylesheet(Stylesheet::Parse(text, name));
    }

    StylesheetId UIRoot::AddStylesheet(Stylesheet sheet)
    {
        for (const StyleDiagnostic& diagnostic : sheet.diagnostics)
        {
            m_state->app.Logger()->Warn("FenrirUI: {}:{}: {}", sheet.name, diagnostic.line, diagnostic.message);
        }
        std::vector<MarkupError> errors;
        const StylesheetId id = AttachStylesheet(std::move(sheet), errors);
        LogErrors(errors);
        return id;
    }

    StylesheetId UIRoot::AttachStylesheet(Stylesheet sheet, std::vector<MarkupError>& errors)
    {
        if (!m_state->hotReload.holdFontFaces)
        {
            Detail::RegisterFontFaces(m_state->roots, m_state->app.TryGetResource<AssetStore>(), sheet, errors);
        }
        auto entry = std::make_unique<Detail::StylesheetEntry>();
        entry->id = {m_state->nextSheet++};
        entry->sheet = std::move(sheet);
        Detail::CompileStylesheet(*m_state, *entry);
        const StylesheetId id = entry->id;
        m_state->sheets.push_back(std::move(entry));
        Detail::RebuildStyleIndex(*m_state);
        MarkSubtreeStyleDirty(*m_state, m_state->rootElement);
        return id;
    }

    bool UIRoot::ReplaceStylesheet(StylesheetId id, Stylesheet sheet, std::vector<MarkupError>& errors)
    {
        auto& sheets = m_state->sheets;
        const auto found =
            std::find_if(sheets.begin(), sheets.end(), [&](const auto& entry) { return entry->id == id; });
        if (!id.IsValid() || found == sheets.end())
        {
            return false;
        }
        Detail::RegisterFontFaces(m_state->roots, m_state->app.TryGetResource<AssetStore>(), sheet, errors);
        auto entry = std::make_unique<Detail::StylesheetEntry>();
        entry->id = id;
        entry->sheet = std::move(sheet);
        Detail::CompileStylesheet(*m_state, *entry);
        *found = std::move(entry);
        Detail::RebuildStyleIndex(*m_state);
        MarkSubtreeStyleDirty(*m_state, m_state->rootElement);
        return true;
    }

    bool UIRoot::RemoveStylesheet(StylesheetId sheet)
    {
        auto& sheets = m_state->sheets;
        const auto found =
            std::find_if(sheets.begin(), sheets.end(), [&](const auto& entry) { return entry->id == sheet; });
        if (!sheet.IsValid() || found == sheets.end())
        {
            return false;
        }
        sheets.erase(found);
        Detail::RebuildStyleIndex(*m_state);
        MarkSubtreeStyleDirty(*m_state, m_state->rootElement);
        return true;
    }

    const Stylesheet* UIRoot::GetStylesheet(StylesheetId sheet) const noexcept
    {
        for (const auto& entry : m_state->sheets)
        {
            if (entry->id == sheet)
            {
                return &entry->sheet;
            }
        }
        return nullptr;
    }

    const ResolvedStyle* UIRoot::GetResolvedStyle(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? &slot->resolved : nullptr;
    }

    const ResolvedStyle* UIRoot::GetPseudoElementStyle(ElementId element, PseudoElement pseudoElement) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        if (slot == nullptr)
        {
            return nullptr;
        }
        const Detail::ScrollbarStyles* bars = slot->scrollbarStyles.get();
        const Detail::TextEntryStyles* entry = slot->textEntryStyles.get();
        switch (pseudoElement)
        {
        case PseudoElement::Scrollbar:
            return bars != nullptr ? &bars->track : nullptr;
        case PseudoElement::ScrollbarThumb:
            return bars != nullptr ? &bars->thumb : nullptr;
        case PseudoElement::Selection:
            return entry != nullptr ? &entry->selection : nullptr;
        case PseudoElement::Placeholder:
            return entry != nullptr ? &entry->placeholder : nullptr;
        case PseudoElement::None:
            break;
        }
        return nullptr;
    }

    ElementBoxes UIRoot::GetBoxes(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? slot->boxes : ElementBoxes{};
    }

    void UIRoot::SetText(ElementId element, std::string_view text)
    {
        Detail::ElementSlot* slot = m_state->Find(element);
        if (slot == nullptr || (slot->text != nullptr && slot->text->Text() == text))
        {
            return;
        }
        if (slot->text == nullptr)
        {
            slot->text = std::make_unique<Detail::ElementText>();
            slot->text->SetStyle(slot->resolved, m_state->textPixelsPerUnit);
            m_state->layout.SetText(slot->node, slot->text.get());
        }
        slot->text->SetText(text);
        m_state->layout.InvalidateMeasure(slot->node);
    }

    std::string_view UIRoot::GetText(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr && slot->text != nullptr ? std::string_view(slot->text->Text()) : std::string_view{};
    }

    const Paragraph* UIRoot::GetParagraph(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr && slot->text != nullptr ? &slot->text->GetParagraph() : nullptr;
    }

    void UIRoot::InvalidateMeasure(ElementId element)
    {
        if (Detail::ElementSlot* slot = m_state->Find(element))
        {
            m_state->layout.InvalidateMeasure(slot->node);
        }
    }

    Math::Vec2 UIRoot::GetScrollOffset(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? slot->scrollOffset : Math::Vec2(0.0f);
    }

    void UIRoot::SetScrollOffset(ElementId element, Math::Vec2 offset)
    {
        Detail::ElementSlot* slot = m_state->Find(element);
        if (slot == nullptr || slot->resolved.overflow != Overflow::Scroll)
        {
            return;
        }
        const Math::Vec2 clamped = glm::clamp(offset, Math::Vec2(0.0f), slot->scrollRange);
        if (clamped != slot->scrollOffset)
        {
            slot->scrollOffset = clamped;
            m_state->boxesDirty = true;
        }
    }

    Math::Vec2 UIRoot::GetScrollRange(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? slot->scrollRange : Math::Vec2(0.0f);
    }

    ScrollbarBoxes UIRoot::GetScrollbars(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr ? Detail::ScrollbarsOf(*slot) : ScrollbarBoxes{};
    }

    void UIRoot::SetFocusable(ElementId element, bool focusable)
    {
        Detail::ElementSlot* slot = m_state->Find(element);
        if (slot == nullptr)
        {
            return;
        }
        slot->focusable = focusable;
        if (!focusable && m_state->input.focused == element)
        {
            Detail::Blur(*this, *m_state);
        }
    }

    bool UIRoot::IsFocusable(ElementId element) const noexcept
    {
        const Detail::ElementSlot* slot = m_state->Find(element);
        return slot != nullptr && slot->focusable;
    }

    bool UIRoot::Focus(ElementId element)
    {
        return Detail::Focus(*this, *m_state, element);
    }

    void UIRoot::Blur()
    {
        Detail::Blur(*this, *m_state);
    }

    ElementId UIRoot::GetFocused() const noexcept
    {
        return m_state->Find(m_state->input.focused) != nullptr ? m_state->input.focused : ElementId{};
    }

    ElementId UIRoot::GetHovered() const noexcept
    {
        return m_state->Find(m_state->input.hovered) != nullptr ? m_state->input.hovered : ElementId{};
    }

    std::optional<Math::Vec2> UIRoot::GetPointer() const noexcept
    {
        return m_state->input.pointer;
    }

    bool UIRoot::TakePress(ElementId element)
    {
        return Detail::TakePress(*this, *m_state, element);
    }

    ElementId UIRoot::HitTest(Math::Vec2 position) const
    {
        return Detail::HitTest(*m_state, position);
    }

    void UIRoot::SetVirtualCursor(bool on)
    {
        Detail::SetVirtualCursor(*this, *m_state, on);
    }

    bool UIRoot::IsVirtualCursorOn() const noexcept
    {
        return m_state->input.cursor.on;
    }

    ElementId UIRoot::GetVirtualCursor() const noexcept
    {
        return m_state->Find(m_state->input.cursor.element) != nullptr ? m_state->input.cursor.element : ElementId{};
    }

    UIListenerId UIRoot::AddListener(ElementId element, UIEventKind kind, UIListener listener)
    {
        Detail::ElementSlot* slot = m_state->Find(element);
        if (slot == nullptr || !listener)
        {
            return {};
        }
        const std::uint32_t serial = m_state->input.nextListener++;
        slot->listeners.push_back({serial, kind, std::make_shared<UIListener>(std::move(listener))});
        return {element, serial};
    }

    bool UIRoot::RemoveListener(UIListenerId listener)
    {
        Detail::ElementSlot* slot = m_state->Find(listener.element);
        if (slot == nullptr)
        {
            return false;
        }
        const auto found =
            std::find_if(slot->listeners.begin(), slot->listeners.end(),
                         [&](const Detail::ListenerEntry& entry) { return entry.serial == listener.serial; });
        if (found == slot->listeners.end())
        {
            return false;
        }
        slot->listeners.erase(found);
        return true;
    }

    void UIRoot::SendEvent(UIEvent event)
    {
        Detail::SendUIEvent(*m_state, event);
    }

    Painter* UIRoot::PaintInto(ElementId element)
    {
        Detail::UIRootState& state = *m_state;
        if (!state.paintStageOpen || state.Find(element) == nullptr)
        {
            return nullptr;
        }
        for (const std::unique_ptr<Detail::StagePaint>& staged : state.stagePaints)
        {
            if (staged->element == element)
            {
                return &*staged->painter;
            }
        }
        return &*Detail::OpenStagePaint(state, element.index).painter;
    }

    void UIRoot::Frame()
    {
        if (LayOut())
        {
            PaintAndSubmit();
        }
    }

    bool UIRoot::LayOut()
    {
        Detail::UIRootState& state = *m_state;
        Detail::StartInput(*this, state);
        state.stats = {};
        state.size = state.target->GetSize(state.app);
        if (state.size.logicalSize.x <= 0.0f || state.size.logicalSize.y <= 0.0f)
        {
            return false;
        }
        const float contentScale = state.ContentScale();
        state.scale = state.config.scaleRule == ScaleRule::Height1080 ? state.size.logicalSize.y / 1080.0f : 1.0f;
        const Math::Vec2 uiSize = state.size.logicalSize / state.scale;
        Detail::FrameInput(*this, state, uiSize);
        PollFiles(state.app.GetTime().deltaTime);
        FontLibrary* fonts = state.roots.GetFonts();
        state.layout.SetFonts(fonts);
        if (const float textPixelsPerUnit = state.scale * contentScale; textPixelsPerUnit != state.textPixelsPerUnit)
        {
            // Text is laid out in target pixels: every paragraph lays out again at the new size.
            state.textPixelsPerUnit = textPixelsPerUnit;
            for (Detail::ElementSlot& slot : state.slots)
            {
                if (slot.alive)
                {
                    UpdateTextStyle(state, slot);
                }
            }
        }

        AdvanceMotion(state, static_cast<float>(state.app.GetTime().deltaTime));
        Detail::ElementSlot& root = state.slots[state.rootElement.index];
        if (root.styleDirty || root.descendantStyleDirty || root.motionDirty)
        {
            ResolveStyles(state, state.rootElement.index, nullptr, false);
        }
        state.layout.SetFixedSize(root.node, uiSize.x, uiSize.y);
        const bool laidOut = state.layout.Layout(root.node);
        state.stats.layouts = laidOut ? 1u : 0u;
        state.stats.measures = state.layout.TakeMeasureCount();

        const float pixelsPerUnit = state.config.snapToPixels ? state.scale * contentScale : 0.0f;
        if (laidOut || state.snappedAt != pixelsPerUnit || state.boxesDirty)
        {
            ReadBoxes(state, state.rootElement.index, 0.0f, 0.0f, {0.0f, 0.0f, uiSize.x, uiSize.y}, pixelsPerUnit);
            state.snappedAt = pixelsPerUnit;
            state.boxesDirty = false;
        }
        if (fonts != nullptr)
        {
            // Each paragraph lays out at its content box's width, to be drawn; one whose fonts changed under it, and
            // its size with them, is measured again next frame.
            for (Detail::ElementSlot& slot : state.slots)
            {
                if (slot.alive && slot.text != nullptr && slot.text->LayOutForDrawing(*fonts))
                {
                    state.layout.InvalidateMeasure(slot.node);
                }
            }
        }

        // The layout that included them has completed: their :intro rules give way next frame, and an intro
        // transition runs from what they gave. With no :intro rule, nothing restyles.
        for (const ElementId element : std::exchange(state.introducing, {}))
        {
            if (Detail::ElementSlot* slot = state.Find(element))
            {
                slot->pseudoClasses =
                    static_cast<std::uint16_t>(slot->pseudoClasses & ~static_cast<std::uint16_t>(PseudoClass::Intro));
                if (state.index.hasIntro)
                {
                    MarkMatchDirty(state, element);
                }
            }
        }
        state.paintStageOpen = true;
        return true;
    }

    void UIRoot::PaintAndSubmit()
    {
        Detail::UIRootState& state = *m_state;
        state.paintStageOpen = false;
        const float contentScale = state.ContentScale();
        DrawList2D list;
        Detail::PaintRoot(state, list, contentScale);
        state.stagePaints.clear();
        state.target->Submit(state.app, std::move(list));

        // Removals wait for painting, so an outro's last frame is seen. Those already gone (under another removal, or
        // removed at once) drop out.
        std::vector<ElementId> waiting;
        for (const ElementId element : std::exchange(state.removing, {}))
        {
            if (state.Find(element) == nullptr)
            {
                continue;
            }
            if (HasTransitions(state, element.index))
            {
                waiting.push_back(element);
            }
            else
            {
                Remove(element, RemoveMode::Immediate);
            }
        }
        state.removing.insert(state.removing.end(), waiting.begin(), waiting.end());
    }
} // namespace Fenrir
