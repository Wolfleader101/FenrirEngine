#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirUI/Element.hpp"
#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/Markup.hpp"
#include "FenrirUI/Style.hpp"
#include "FenrirUI/Stylesheet.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "FenrirUI/UITarget.hpp"

namespace Fenrir
{
    class App;
    class Painter;
    class Paragraph;
    class UIRoots;

    namespace Detail
    {
        /** @brief A root's tree, Yoga nodes and frame state. Defined in FenrirUI's sources. */
        struct UIRootState;
        /** @brief What builds a markup document's elements into a root. Defined in FenrirUI's sources. */
        struct MarkupLoader;
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
        /**
         * @brief How its UI units map to its target's logical units; unset, its target's default
         * (UITarget::GetDefaultScaleRule): `Height1080` on the window and in an image, `ContentScale` on a world quad,
         * whose logical units are UI units. GetConfig gives the one it lays out by.
         */
        std::optional<ScaleRule> scaleRule;
        /**
         * @brief The blend space it paints in; unset, its target's default (UITarget::GetDefaultBlendSpace): gamma on
         * the window overlay, as browsers and designers blend, and linear in an image (ADR-0065). GetConfig gives the
         * one it paints in.
         */
        std::optional<BlendSpace2D> blendSpace;
        /** @brief Snap laid-out rectangles to the target's whole pixels. */
        bool snapToPixels = true;
        /**
         * @brief Where it draws instead of its window's overlay; null for the overlay. A UIImageTarget draws it into an
         * image, and makes it a root with no input router: its input comes from code. A UIWorldTarget draws it on a
         * quad in the world, its pointer the ray the game gives it (UIRoot::SetPointerRay).
         */
        std::shared_ptr<UITarget> target;
        /**
         * @brief Its input router's priority (ADR-0027): routers run highest first, so give a root drawn over another
         * a higher one. Equal priorities run in the order the roots were made.
         */
        int inputPriority = 0;
        /**
         * @brief Give it FenrirUI's default stylesheet (DefaultStylesheet) before its own sheets: a rule of the root's
         * sheets wins over one of the default's of the same or lower specificity, by coming later, and loses to a more
         * specific one, as CSS orders them. Off, the controls have no look but what the root's sheets give them.
         */
        bool defaultStylesheet = true;
        /**
         * @brief Start with the virtual cursor on (UIRoot::SetVirtualCursor): the left stick moves the pointer and A
         * presses and releases the left button under it.
         */
        bool virtualCursor = false;
        /** @brief How fast the virtual cursor moves with the stick pushed all the way, in UI units a second. */
        float virtualCursorSpeed = 900.0f;
        /** @brief Start with hot reload on (UIRoot::SetHotReload): the files the root loaded are polled for changes. */
        bool hotReload = false;
    };

    /**
     * @brief Where an element's scrollbars are, in UI units from the root's top-left, as of the last frame's layout:
     * each bar's track along an edge of its padding box and its thumb inside the track. A bar shows while its content
     * is longer than the padding box on its axis and its `::scrollbar` gives it a width.
     */
    struct ScrollbarBoxes
    {
        /** @brief Whether the vertical bar, along the right edge, shows. */
        bool vertical = false;
        /** @brief Whether the horizontal bar, along the bottom edge, shows. */
        bool horizontal = false;
        /** @brief The vertical bar's track and its thumb, while it shows. */
        Rect2D verticalTrack;
        Rect2D verticalThumb;
        /** @brief The horizontal bar's track and its thumb, while it shows. */
        Rect2D horizontalTrack;
        Rect2D horizontalThumb;
        /** @brief How far each thumb moves from one end of its track to the other: x the horizontal one's, y the
         * vertical one's. */
        Math::Vec2 travel{0.0f, 0.0f};
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
        /** @brief Files whose modification time hot reload read: none but on a frame that polled. */
        std::uint32_t filesPolled = 0;
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
        /** @brief How far the left stick must lean along an axis, of 1, before it moves the focus that way. */
        static constexpr float NavigationDeadZone = 0.5f;
        /** @brief Seconds a direction is held before the focus moves again. */
        static constexpr double NavigationRepeatDelay = 0.4;
        /** @brief Seconds between the moves after that while it is still held. */
        static constexpr double NavigationRepeatInterval = 0.12;
        /** @brief How far the left stick must lean, of 1, before it moves the virtual cursor. */
        static constexpr float VirtualCursorDeadZone = 0.15f;
        /** @brief Seconds of FenrirTime between hot reload's polls of the files the root loaded. */
        static constexpr double HotReloadInterval = 0.25;

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

        /** @brief What it was made with, its blend space set to the one it paints in. */
        [[nodiscard]] const UIRootConfig& GetConfig() const noexcept;
        /** @brief Its id, which its UIEvents carry. */
        [[nodiscard]] UIRootId GetId() const noexcept;
        /** @brief Logical units per UI unit, by its scale rule, as of its last frame (1 before its first). */
        [[nodiscard]] float GetScale() const noexcept;
        /** @brief Its target's size as of its last frame. */
        [[nodiscard]] UITargetSize GetTargetSize() const noexcept;
        /** @brief What its last frame did. */
        [[nodiscard]] const UIRootStats& GetStats() const noexcept;
        /** @brief The app it is in, for elements that read its resources. */
        [[nodiscard]] App& GetApp() const noexcept;

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
         * filter and scrollbar properties, which a stylesheet parses and keeps), or a value it cannot read is
         * logged, with both named, and changes nothing: false.
         */
        bool SetStyleProperty(ElementId element, std::string_view name, std::string_view value);
        /**
         * @brief Parses `text` as a stylesheet named `name` (Stylesheet::Parse), logs what the parser refused with the
         * name and line, and adds it after the root's other sheets: at equal specificity a later sheet's rules win.
         * Every element's style is resolved again on the next frame. The sheet's url()s load from the asset store now;
         * a declaration whose image cannot be loaded is logged and skipped. Its `@font-face`s register their faces
         * (UIRoots::AddFontFace); one refused is logged. Its `@import`s load nothing: LoadStylesheet loads them.
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
        /**
         * @brief The style of one of its pseudo-elements as of the last frame, inheriting from it and matching its
         * pseudo-classes: a scroll view's `::scrollbar` and `::scrollbar-thumb` while it has `overflow: scroll`, a
         * text entry's `::selection` and `::placeholder`. Null for one it does not have, or a stale id.
         */
        [[nodiscard]] const ResolvedStyle* GetPseudoElementStyle(ElementId element,
                                                                 PseudoElement pseudoElement) const noexcept;
        /** @brief Its rectangles as of the last frame's layout; empty rectangles for a stale id. */
        [[nodiscard]] ElementBoxes GetBoxes(ElementId element) const noexcept;
        /**
         * @brief Gives `element` the UTF-8 `text`, any element as a label has it: a FenrirText paragraph kept on the
         * element, in its text properties (color, font-family, font-size, font-weight, font-style, text-align,
         * line-height, letter-spacing, white-space and text-overflow), laid out again when the text, those or the
         * width it is given change. While it has no children, layout measures its content box to the paragraph at
         * the width it is offered; each frame paints it at the content box's top-left, under its text-shadow, before
         * the element's own Paint. The paragraph is laid out in the target's pixels, its sizes the style's times the
         * root's scale and the target's content scale, and measured back in UI units. The fonts are the app's
         * FontLibrary (UIRoots::GetFonts); without one the text measures and draws nothing.
         */
        void SetText(ElementId element, std::string_view text);
        /** @brief Its text; empty when it has none or for a stale id. */
        [[nodiscard]] std::string_view GetText(ElementId element) const noexcept;
        /**
         * @brief Its paragraph as of the last frame, laid out in the target's pixels at its content box's width; null
         * when it has no text or for a stale id. Valid until the element is removed.
         */
        [[nodiscard]] const Paragraph* GetParagraph(ElementId element) const noexcept;
        /** @brief Has layout measure `element` again next frame, when its content's size changed. */
        void InvalidateMeasure(ElementId element);
        /** @} */

        /** @name Markup documents
         * @{
         */
        /**
         * @brief Loads the markup document `text` (UTF-8), named `name` in its errors, under `parent`, after its
         * children (ADR-0064): structure with no logic. Each load makes elements of its own.
         *
         * - **Tags** are the registered element tags (UIRoots::IsRegistered: `panel`, `label`, the controls and any
         *   custom element), each made through its factory as Create makes it, in document order, with `<tag/>` for
         *   one with nothing in it. Names are case-sensitive.
         * - **Attributes** are `name="value"` (or single quotes), or a bare name for an empty value. Every element
         *   takes `id` (SetIdString), `class` (classes split on whitespace), `style` (inline declarations, as
         *   SetStyleProperty reads them) and `focusable` (`true` or `false`, applied after the element's OnCreate and
         * its other attributes, so it wins over what `disabled` does to the focus); the rest go to
         * Element::SetAttribute, which each control parses (Controls.hpp).
         * - **Text content** between tags, with the whitespace at its ends trimmed, is the attribute `text`, which a
         *   `label` and a `button` take; text in an element that takes none, or in one with a `text` attribute, is an
         * error.
         * - `<include src="...">` loads the document the resolver gives for its src in its place, its top-level
         *   elements under the include's parent; an include back into a document being loaded is an error.
         * - `<style>` holds a stylesheet and `<style src="...">` names one by the resolver; either is parsed
         *   (Stylesheet::Parse), its `@import`s loaded in place by the resolver, and added after the root's sheets
         *   (AddStylesheet), and its errors are the load's.
         *   Inside an inline `<style>` text is CSS as written: no entities, and it ends at `</style>`.
         * - `&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`, `&#N;` and `&#xN;` decode in text and attribute values.
         * - `<!-- -->` comments are skipped. Namespaces, processing instructions, DTDs and CDATA are errors.
         *
         * An unknown tag (with what is in it), an unknown attribute or one whose value its element refuses, text where
         * none is taken, a bad entity, a tag left open or a closing tag that closes none, an unquoted or repeated
         * attribute, attributes with no whitespace between them, an empty `<style>`, and every error of a sheet are
         * errors with their file and line, logged as warnings too; the rest of the document loads. A stale parent loads
         * nothing. The resolver may be empty when the document includes nothing.
         */
        MarkupLoad LoadMarkup(ElementId parent, std::string_view text, std::string_view name,
                              const MarkupResolver& resolver = {});
        /**
         * @brief Loads the markup document at `path` under `parent`, as LoadMarkup loads one, from files: `path` is
         * relative to the asset root (or absolute), and an `<include src>` or `<style src>` is relative to the file it
         * is written in, as an `@import` is to its sheet. The errors name each file by that path. A document that is
         * not there is one error and loads nothing. The root remembers the files it read for hot reload, the load
         * having errors or not.
         */
        MarkupLoad LoadDocument(ElementId parent, std::string_view path);
        /**
         * @brief Loads the stylesheet at `path`, relative to the asset root (or absolute), and adds it as
         * AddStylesheet adds one; its `@import`s load as LoadDocument's sheets' do. What it refused is logged with its
         * file and line. Nothing, logged, for a file that is not there. The root remembers its files for hot reload.
         */
        StylesheetId LoadStylesheet(std::string_view path);
        /**
         * @brief Turns hot reload on or off. On, every HotReloadInterval of FenrirTime the root reads the modification
         * time of every file it loaded (documents, includes, sheets and imports) and loads again what changed:
         *
         * - A changed sheet or import is parsed again in its sheet's place, and every style resolved again; its
         *   elements stay as they are.
         * - A changed document or include loads its document again under the same parent at the same position: the
         *   elements it made before, and its sheets, are removed at once, their ids going stale, and DocumentReloaded
         *   is sent at the new first element (at the parent when it made none) for the game to find its ids again.
         * - A version with any error, or a file that is gone, is not taken: the one before stays, and the errors are
         *   logged. It is read again when it next changes.
         *
         * A document none of whose elements is alive any more, or whose parent is not, and a sheet that was removed,
         * are not polled again. Off, no file is polled.
         */
        void SetHotReload(bool on);
        /** @brief Whether hot reload is on. */
        [[nodiscard]] bool IsHotReloadOn() const noexcept;
        /** @} */

        /** @name Scrolling
         * An element with `overflow: scroll` clips its children to its padding box, as `hidden` does, and moves them
         * up and left by its scroll offset: they are laid out, painted and hit-tested where the offset puts them. Its
         * range is how far its content (its children's border boxes, and its padding past them) reaches past its
         * padding box; the offset is clamped to it after each layout.
         * @{
         */
        /** @brief Its scroll offset; zero for an element that does not scroll or a stale id. */
        [[nodiscard]] Math::Vec2 GetScrollOffset(ElementId element) const noexcept;
        /**
         * @brief Scrolls it to `offset`, clamped to its range as of the last layout (and again after the next): its
         * children move on the next frame. Nothing for an element that does not scroll.
         */
        void SetScrollOffset(ElementId element, Math::Vec2 offset);
        /** @brief The largest offset it can scroll to, as of the last layout; zero when its content fits. */
        [[nodiscard]] Math::Vec2 GetScrollRange(ElementId element) const noexcept;
        /** @brief Its scrollbars as of the last layout; none for an element that does not scroll. */
        [[nodiscard]] ScrollbarBoxes GetScrollbars(ElementId element) const noexcept;
        /** @} */

        /** @name Input and focus
         * A gamepad moves the focus too (ADR-0065). While one of the root's elements has the focus, the D-pad, and the
         * left stick past NavigationDeadZone, are directions: each is first offered to the focused element alone as a
         * GamepadButtonEvent press of the D-pad button that way (a slider steps, a scroll view scrolls), and when it
         * does not take it the focus moves to the focusable element nearest that way, among those with nothing
         * focusable under them. The nearest is Unity's and Godot's rule: of the elements whose centre lies past the
         * focused one's far edge, the least distance along the direction plus twice the gap across it, less how much
         * the two overlap across it; equal ones by tree order. Held, a direction repeats after NavigationRepeatDelay
         * every NavigationRepeatInterval. When no root of the app has the focus, the first direction gives it to the
         * root's first focusable element in tree order. A (the South button) is an Enter press at the focused element,
         * so it activates a control as Enter does, and B (East) sends Cancel at it. Each scroll view above an element
         * the focus moves to by direction or Tab scrolls just far enough to show its border box. The root consumes the
         * buttons and the left stick it acts on, and claims the gamepad, only while it has the focus or its virtual
         * cursor is on; the rest reaches the game.
         * @{
         */
        /**
         * @brief Makes `element` focusable or not: whether a click on it or under it, Tab and Shift+Tab, a gamepad
         * direction and Focus can give it the focus. Elements are not focusable until made so (the controls make
         * themselves focusable). Making the focused element unfocusable blurs it.
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
         * @brief Where the pointer last was in the root's window, in UI units; none before it moved there. On a world
         * root, where the pointer ray last met the quad; none while it misses.
         */
        [[nodiscard]] std::optional<Math::Vec2> GetPointer() const noexcept;
        /**
         * @brief Gives a world root (UITarget::TakesPointerRay, a UIWorldTarget's) its pointer for this frame: `ray`,
         * in world space, from whatever camera and cursor, or controller, the game chooses (ADR-0057's line), and
         * whether its left button is `pressed`; none when there is no pointer. The root meets the ray with its quad,
         * from either side, and runs hit testing, hover, press, click and focus at that point in UI units, as the
         * window's mouse does on an overlay root: a change of `pressed` is a press or a release there. A ray that
         * misses the quad, or none, hovers nothing and leaves the root with no pointer, but while a press is held the
         * pointer follows the ray across the quad's plane past its edges, so a drag goes on; with no ray, or one that
         * never meets the plane, the held press is released as cancelled, with no click, whatever `pressed` says. A
         * press with nothing under it takes the focus from the root, as a click off an overlay root does; one on an
         * element blurs the window's other roots.
         *
         * Returns whether the pointer is over one of the root's elements that takes pointer input, or a press is held
         * on one. On any other root it does nothing and returns false. Call it once a frame, after FenrirInput's
         * dispatch (in an Update system, say), with the ray as the camera sees it that frame; the layout it hits is the
         * last frame's, as the mouse's is.
         */
        bool SetPointerRay(const std::optional<Math::Ray>& ray, bool pressed);
        /**
         * @brief Gives the buttons held now to `element`, as a scroll view does when a press on its content becomes a
         * drag: what they were pressed on loses them (and `:active`), their motion and releases go to `element`, and
         * their releases send no click. False, doing nothing, for a stale element or while no button is held.
         */
        bool TakePress(ElementId element);
        /**
         * @brief The topmost element at `position` (UI units from the root's top-left) that takes pointer input
         * (`pointer-events: all`), as of the last frame's layout: children before their parent, siblings by z-index
         * then tree order, inside rounded corners, under transforms, within overflow clips. Nothing when none is.
         */
        [[nodiscard]] ElementId HitTest(Math::Vec2 position) const;
        /**
         * @brief Turns the virtual cursor on or off. On, the left stick moves the pointer (beyond
         * VirtualCursorDeadZone, up to UIRootConfig::virtualCursorSpeed, kept inside the root) as the mouse would,
         * hover and drags included, and A presses and releases the left mouse button at it (a press with no UI under it
         * is the game's, as the mouse's is, and A presses nothing while a mouse button is held); it starts at the
         * root's centre when the pointer has not been anywhere yet. Each frame it is on, the root keeps a
         * `virtual-cursor` element, the root element's child (its last when made), placed with its left and top at the
         * pointer, above everything (`z-index` the largest) and taking no pointer input; the stylesheet draws it. Off,
         * its element is removed at once and the stick is the game's.
         */
        void SetVirtualCursor(bool on);
        /** @brief Whether the virtual cursor is on. */
        [[nodiscard]] bool IsVirtualCursorOn() const noexcept;
        /** @brief The virtual cursor's element; nothing while it is off or before the first frame it was on. */
        [[nodiscard]] ElementId GetVirtualCursor() const noexcept;
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
         * @brief A painter over a list of `element`'s own for this frame, for work in the paint stage
         * (UIRoots::AddPaintStage) and the C# systems of `Fenrir.UI.Paint`: what it draws is painted after the
         * element's own boxes, text and Paint, and before its children, as part of the element (ADR-0061). It draws
         * in the root's UI units, the units GetBoxes gives the element's rectangles in, under the element's transform
         * and inside the clips the element's content is drawn in, faded with the element's content when its opacity
         * fades it box by box; a list that leaves a group or a mask open is not drawn, and is logged; an element that
         * does not paint (display none, or under one) drops it. Calls for one element in one frame give one painter,
         * good until the stage ends. Null outside the paint stage, for a stale id, or on a frame the root's target has
         * no size.
         */
        [[nodiscard]] Painter* PaintInto(ElementId element);

        /**
         * @brief Runs one frame of the root: resolves the dirty styles, lays out if anything laid out is dirty,
         * snaps the rectangles, paints every element and submits the list to the target. UIRoots runs it for every
         * root in PostUpdate, in two halves with the paint stage between them.
         */
        void Frame();

      private:
        friend class UIRoots;
        friend struct Detail::MarkupLoader;

        /**
         * @brief Frame's first half: input, hot reload, motion, styles and layout. False on a frame its target has no
         * size, when the second half does nothing either; true opens PaintInto until PaintAndSubmit.
         */
        bool LayOut();
        /** @brief Frame's second half: paints every element and the paint stage's lists, submits, then removes outros.
         */
        void PaintAndSubmit();
        /** @brief Leaves FenrirInput before the root is freed, which UIRoots puts off to its next frame. */
        void Retire();
        /** @brief SetStyleProperty without the log: false with why in `error`. */
        bool ApplyStyleProperty(ElementId element, std::string_view name, std::string_view value, std::string& error);
        /**
         * @brief AddStylesheet(Stylesheet) without the log: registers the sheet's font faces, with what they refused in
         * `errors`, and adds it.
         */
        StylesheetId AttachStylesheet(Stylesheet sheet, std::vector<MarkupError>& errors);
        /** @brief Puts `sheet` in the place of the sheet `id`, as AttachStylesheet adds one. False when none is `id`.
         */
        bool ReplaceStylesheet(StylesheetId id, Stylesheet sheet, std::vector<MarkupError>& errors);
        /** @brief Hot reload's poll, when it is on and its interval has passed. */
        void PollFiles(double deltaTime);
        /** @brief Loads the document hot reload remembers at `index` again, taking it unless it has errors. */
        void ReloadDocument(std::size_t index);
        /** @brief Parses the sheet hot reload remembers at `index` again, taking it unless it has errors. */
        void ReloadStylesheet(std::size_t index);
        /** @brief Logs a load's errors with their files and lines. */
        void LogErrors(std::span<const MarkupError> errors) const;

        std::unique_ptr<Detail::UIRootState> m_state;
    };
} // namespace Fenrir
