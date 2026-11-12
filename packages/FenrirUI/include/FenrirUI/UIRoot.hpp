#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirUI/Element.hpp"
#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/Style.hpp"
#include "FenrirUI/Stylesheet.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "FenrirUI/UITarget.hpp"

namespace Fenrir
{
    class App;
    class UIRoots;

    namespace Detail
    {
        /** @brief A root's tree, Yoga nodes and frame state. Defined in FenrirUI's sources. */
        struct UIRootState;
    } // namespace Detail

    /** @brief How a UI root maps its UI units to its target's logical units (the scale rule). */
    enum class ScaleRule : std::uint8_t
    {
        /**
         * @brief 1080 UI units span the target's pixel height: scale = pixel height / 1080 / content scale logical
         * units per UI unit, so a UI authored at 1080p keeps its proportions at every resolution.
         */
        Height1080,
        /** @brief One UI unit is one logical unit, which follows the operating system's content scale. */
        ContentScale,
    };

    /**
     * @brief A pseudo-class an element carries, as bit flags; the structural ones (first and last child) are not. The
     * root sets Intro on each element it makes, until the first layout that includes it completes, and Outro on an
     * element Remove() waits to remove.
     */
    enum class PseudoClass : std::uint16_t
    {
        Hover = 1u << 0u,
        Active = 1u << 1u,
        Focus = 1u << 2u,
        FocusWithin = 1u << 3u,
        Disabled = 1u << 4u,
        Checked = 1u << 5u,
        Intro = 1u << 6u,
        Outro = 1u << 7u,
    };

    /** @brief How UIRoot::Remove takes an element away. */
    enum class RemoveMode : std::uint8_t
    {
        /** @brief Give it `:outro`, and remove it once its and its descendants' transitions have ended (ADR-0066). */
        Outro,
        /** @brief Remove it and everything under it at once. */
        Immediate,
    };

    /** @brief What a UI root is made with. */
    struct UIRootConfig
    {
        /** @brief The window whose overlay it draws on (ADR-0027), when it has no target of its own. */
        WindowId window{1};
        /** @brief The overlay layer it draws on; roots draw lowest layer first, then in the order they were made. */
        std::int32_t layer = 0;
        /** @brief How its UI units map to its target's logical units. */
        ScaleRule scaleRule = ScaleRule::Height1080;
        /** @brief Blending on the window overlay is in gamma space by default, as browsers and designers do (ADR-0065).
         */
        BlendSpace2D blendSpace = BlendSpace2D::Gamma;
        /** @brief Snap laid-out rectangles to the target's whole pixels. */
        bool snapToPixels = true;
        /** @brief Where it draws instead of its window's overlay; null for the overlay. */
        std::shared_ptr<UITarget> target;
        /**
         * @brief Its input router's priority (ADR-0027): routers run highest first, so give a root drawn over another
         * a higher one. Equal priorities run in the order the roots were made.
         */
        int inputPriority = 0;
    };

    /** @brief What a root's last frame did: how much of the tree it styled, laid out and painted. */
    struct UIRootStats
    {
        /** @brief Elements whose style was resolved. */
        std::uint32_t stylesResolved = 0;
        /** @brief Yoga layouts run: 1 when anything laid out was dirty, 0 for a clean root. */
        std::uint32_t layouts = 0;
        /** @brief Measure calls made to elements. */
        std::uint32_t measures = 0;
        /** @brief Elements painted. */
        std::uint32_t elementsPainted = 0;
    };

    /**
     * @brief A UI root (ADR-0063, ADR-0066): an element tree of its own, the 2D target it draws into, and its scale
     * rule. Made and destroyed through the UIRoots app resource. It starts with one element, its root element, which
     * fills the target and cannot be removed.
     *
     * Elements are addressed by ElementId; every call taking a stale id does nothing and returns an empty value. A
     * change to an element's classes, pseudo-classes or inline style marks its style dirty, and a change to its
     * children marks its layout dirty; each frame (in PostUpdate) the root moves its transitions and animations on by
     * the frame's delta (FenrirTime's), resolves the dirty styles, writes the transitions and animations over them,
     * lays out with Yoga if anything laid out changed, snaps the rectangles to pixels, clears `:intro`, paints every
     * element in tree order through the painter, submits the list to its target, and then removes the elements whose
     * outros have ended. A frame whose target has no size does none of it, so time stands still for it. Call it outside
     * parallel systems.
     */
    class UIRoot
    {
      public:
        /** @brief Where Create puts an element when no index is given: after its last sibling. */
        static constexpr std::size_t End = std::numeric_limits<std::size_t>::max();

        /**
         * @brief A root of `roots` in `app`, named `id`; UIRoots::CreateRoot makes them. With FenrirInput installed, it
         * registers its input router at once, or else on its first frame after Input::Install.
         */
        UIRoot(App& app, UIRoots& roots, UIRootConfig config, UIRootId id);
        UIRoot(const UIRoot&) = delete;
        UIRoot& operator=(const UIRoot&) = delete;
        UIRoot(UIRoot&&) = delete;
        UIRoot& operator=(UIRoot&&) = delete;
        ~UIRoot();

        /** @brief What it was made with. */
        [[nodiscard]] const UIRootConfig& GetConfig() const noexcept;
        /** @brief Its id, which its UIEvents carry. */
        [[nodiscard]] UIRootId GetId() const noexcept;
        /** @brief Logical units per UI unit, by its scale rule, as of its last frame (1 before its first). */
        [[nodiscard]] float GetScale() const noexcept;
        /** @brief Its target's size as of its last frame. */
        [[nodiscard]] UITargetSize GetTargetSize() const noexcept;
        /** @brief What its last frame did. */
        [[nodiscard]] const UIRootStats& GetStats() const noexcept;

        /** @name The tree
         * @{
         */
        /** @brief The root element, which fills the target. */
        [[nodiscard]] ElementId GetRootElement() const noexcept;
        /**
         * @brief Makes an element of `tag` under `parent`, at `index` among its children (End, or past the last, for
         * after them), through the factory registered for `tag`; an unregistered tag makes a plain Element. It carries
         * `:intro` until the first layout that includes it completes, and its first style starts no transitions. A
         * stale parent makes nothing and returns an id that names nothing.
         */
        ElementId Create(ElementId parent, std::string_view tag, std::size_t index = End);
        /**
         * @brief Removes `element` and everything under it. By default it gives the element `:outro` and removes it
         * after the frame in which no transition runs on it or under it any more (a frame's styles are resolved
         * first, so the outro's own transitions count): until then it is alive, laid out, painted and hit-tested, and
         * a second call does nothing more. RemoveMode::Immediate removes it at once, an outro under way or not. The
         * ids go stale at the removal. The root element cannot be removed. False when there was nothing to remove.
         */
        bool Remove(ElementId element, RemoveMode mode = RemoveMode::Outro);
        /**
         * @brief Moves `element` (with everything under it) to be `newParent`'s child at `index` (End for last). False,
         * changing nothing, for a stale id, the root element, or a new parent inside `element`.
         */
        bool Reparent(ElementId element, ElementId newParent, std::size_t index = End);
        /** @brief Moves `element` to `index` among its siblings (End, or past the last, for last). */
        bool Reorder(ElementId element, std::size_t index);
        /** @brief Whether `element` names an element of this root that has not been removed. */
        [[nodiscard]] bool IsAlive(ElementId element) const noexcept;
        /** @brief Its parent; nothing for the root element or a stale id. */
        [[nodiscard]] ElementId GetParent(ElementId element) const noexcept;
        /**
         * @brief Its children in order; empty for a stale id. Like the tag, id string and classes below, the view is
         * valid until the tree next changes.
         */
        [[nodiscard]] std::span<const ElementId> GetChildren(ElementId element) const noexcept;
        /** @brief Its behaviour object; null for a stale id. */
        [[nodiscard]] Element* GetElement(ElementId element) const noexcept;
        /** @} */

        /** @name Names and queries
         * @{
         */
        /** @brief Its tag name; empty for a stale id. */
        [[nodiscard]] std::string_view GetTag(ElementId element) const noexcept;
        /** @brief Its id string; empty when it has none. */
        [[nodiscard]] std::string_view GetIdString(ElementId element) const noexcept;
        /** @brief Sets its id string. */
        void SetIdString(ElementId element, std::string_view id);
        /** @brief The first element in tree order whose id string is `id`; nothing when none is. */
        [[nodiscard]] ElementId FindById(std::string_view id) const;
        /** @brief Every element in tree order that has the class `name`. */
        [[nodiscard]] std::vector<ElementId> FindByClass(std::string_view name) const;
        /** @brief Its classes, in the order added. */
        [[nodiscard]] std::span<const std::string> GetClasses(ElementId element) const noexcept;
        /** @brief Adds the class `name` unless it has it. */
        void AddClass(ElementId element, std::string_view name);
        /** @brief Removes the class `name`. */
        void RemoveClass(ElementId element, std::string_view name);
        /** @brief Whether it has the class `name`. */
        [[nodiscard]] bool HasClass(ElementId element, std::string_view name) const;
        /** @brief Sets or clears a pseudo-class. */
        void SetPseudoClass(ElementId element, PseudoClass pseudoClass, bool on);
        /** @brief Whether it carries a pseudo-class. */
        [[nodiscard]] bool HasPseudoClass(ElementId element, PseudoClass pseudoClass) const noexcept;
        /** @} */

        /** @name Style and layout
         * @{
         */
        /** @brief Its inline style; null for a stale id. */
        [[nodiscard]] const StyleDeclarations* GetInlineStyle(ElementId element) const noexcept;
        /**
         * @brief Its inline style to change, through the typed setters; marks its style dirty. Null for a stale id. The
         * pointer is for the change at hand: it is not kept valid across a Create.
         */
        [[nodiscard]] StyleDeclarations* EditInlineStyle(ElementId element);
        /**
         * @brief Gives the inline style property `name` (a CSS longhand, such as `margin-left`, or a shorthand a
         * stylesheet takes, such as `margin`, which gives all of its longhands or none) the value `value`, as CSS
         * writes it: lengths in `px` or `%` (`0` alone is 0px) or `auto`, colours as `#rgb`, `#rgba`, `#rrggbb`,
         * `#rrggbbaa`, `rgb()`, `rgba()`, `hsl()`, `hsla()` or a CSS colour name, `url(path)` for an image asset loaded
         * from the asset store, `linear-gradient()` and `radial-gradient()`, `box-shadow` lists, and `transform`
         * lists of `translate()`, `translateX()`, `translateY()`, `scale()`, `scaleX()`, `scaleY()`, `rotate()` (deg,
         * rad, grad or turn), times in `s` or `ms` and timing functions (the CSS named set, `cubic-bezier()`,
         * `steps()`) in the transition and animation lists. An unknown property, one the style does not hold yet (the
         * text, filter and scrollbar properties, which a stylesheet parses and keeps), or a value it cannot read is
         * logged, with both named, and changes nothing: false.
         */
        bool SetStyleProperty(ElementId element, std::string_view name, std::string_view value);
        /**
         * @brief Parses `text` as a stylesheet named `name` (Stylesheet::Parse), logs what the parser refused with the
         * name and line, and adds it after the root's other sheets: at equal specificity a later sheet's rules win.
         * Every element's style is resolved again on the next frame. The sheet's url()s load from the asset store now;
         * a declaration whose image cannot be loaded is logged and skipped.
         */
        StylesheetId AddStylesheet(std::string_view text, std::string_view name);
        /** @brief Adds a sheet already parsed, as AddStylesheet(text, name) does after parsing. */
        StylesheetId AddStylesheet(Stylesheet sheet);
        /** @brief Takes a sheet away and resolves every style again next frame. False when it names none of the root's.
         */
        bool RemoveStylesheet(StylesheetId sheet);
        /** @brief A sheet the root holds, as parsed; null when it names none. */
        [[nodiscard]] const Stylesheet* GetStylesheet(StylesheetId sheet) const noexcept;
        /**
         * @brief Its resolved style as of the last frame, with its transitions and animations over the cascade: what
         * it was painted with. Null for a stale id.
         */
        [[nodiscard]] const ResolvedStyle* GetResolvedStyle(ElementId element) const noexcept;
        /** @brief Its rectangles as of the last frame's layout; empty rectangles for a stale id. */
        [[nodiscard]] ElementBoxes GetBoxes(ElementId element) const noexcept;
        /** @brief Has layout measure `element` again next frame, when its content's size changed. */
        void InvalidateMeasure(ElementId element);
        /** @} */

        /** @name Input and focus
         * @{
         */
        /**
         * @brief Makes `element` focusable or not: whether a click on it or under it, Tab and Shift+Tab, and Focus can
         * give it the focus. Elements are not focusable until made so (the controls make themselves focusable).
         * Making the focused element unfocusable blurs it.
         */
        void SetFocusable(ElementId element, bool focusable);
        /** @brief Whether it is focusable. */
        [[nodiscard]] bool IsFocusable(ElementId element) const noexcept;
        /**
         * @brief Gives `element` the focus: it gets `:focus` and its ancestors `:focus-within`, the element that had it
         * is sent Blur and it is sent Focus, and the root claims keyboard focus in its window, so keys and text go to
         * it. False, changing nothing, for a stale or unfocusable element.
         */
        bool Focus(ElementId element);
        /** @brief Takes the focus from whichever element has it, sending it Blur. Nothing has it after. */
        void Blur();
        /** @brief The element that has the focus; nothing when none has. */
        [[nodiscard]] ElementId GetFocused() const noexcept;
        /** @brief The topmost element under the pointer that takes pointer input; nothing when none is. */
        [[nodiscard]] ElementId GetHovered() const noexcept;
        /**
         * @brief The topmost element at `position` (UI units from the root's top-left) that takes pointer input
         * (`pointer-events: all`), as of the last frame's layout: children before their parent, siblings by z-index
         * then tree order, inside rounded corners, under transforms, within overflow clips. Nothing when none is.
         */
        [[nodiscard]] ElementId HitTest(Math::Vec2 position) const;
        /**
         * @brief Attaches `listener` to `element` for UIEvents of `kind`: it runs when one happens to the element or
         * bubbles up to it, after the listeners of the elements under it and, on one element, in the order added. It
         * lives until RemoveListener or the element is removed. Returns an id that names nothing for a stale element or
         * an empty listener.
         */
        UIListenerId AddListener(ElementId element, UIEventKind kind, UIListener listener);
        /** @brief Detaches a listener. False when it names none. A dispatch already under way still runs it. */
        bool RemoveListener(UIListenerId listener);
        /**
         * @brief Sends `event` (its root set to this one): once to the engine event queue, then to the listeners of its
         * kind on its target and on each ancestor in turn, until one stops it. The router sends the pointer, key,
         * text and focus kinds; controls send Change and Submit through it. Nothing for a stale target.
         */
        void SendEvent(UIEvent event);
        /** @} */

        /**
         * @brief Runs one frame of the root: resolves the dirty styles, lays out if anything laid out is dirty,
         * snaps the rectangles, paints every element and submits the list to the target. UIRoots runs it for every
         * root in PostUpdate.
         */
        void Frame();

      private:
        friend class UIRoots;

        /** @brief Leaves FenrirInput before the root is freed, which UIRoots puts off to its next frame. */
        void Retire();

        std::unique_ptr<Detail::UIRootState> m_state;
    };
} // namespace Fenrir
