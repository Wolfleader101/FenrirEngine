#include "FenrirUIScripting/UIScripting.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirPainterScripting/PainterScriptingState.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/Element.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "FenrirUIScripting/ScriptUI.hpp"
#include "Generated/FenrirUINatives.hpp"
#include "UIScriptingState.hpp"

// Everything C# sees of the UI is generated (api/FenrirUI.binding.toml): the mirrors of ScriptUI.hpp's settings and of
// FenrirUI's plain data (ElementId, UIRootId as UIRoot, UIEvent, ElementBoxes and the enums), their mappings, checked
// against the Fenrir.UI contract whenever a script assembly loads, the UIStages class of the stage it hosts, and from
// the native functions below (described into api/FenrirUI.natives.json) their C# declarations, UINative, the UIRoot
// mirror's methods, and this registration of them all. The paint stage's groups are placed by hand, at the end.

namespace Fenrir
{
    namespace
    {
        /** @brief A script's index among siblings: negative for after the last, as UIRoot::End. */
        std::size_t IndexOf(std::int32_t index)
        {
            return index < 0 ? UIRoot::End : static_cast<std::size_t>(index);
        }

        /** @brief Writes `ids` into C#'s span, as much as fits, and returns how many there are. */
        std::int32_t WriteIds(std::span<const ElementId> ids, ElementId* out, std::int32_t count)
        {
            const std::span<ElementId> room = SpanOf(out, count);
            std::copy_n(ids.begin(), std::min(room.size(), ids.size()), room.begin());
            return static_cast<std::int32_t>(ids.size());
        }
    } // namespace

    // The native functions behind Fenrir.UI's UIRoot, each a UIRoot call of the same name on the root a script names.
    namespace UINatives
    {
        /**
         * @brief Makes a UI root on a window's overlay, which lives until it is destroyed or the UI ends.
         * @param[out] root The root made.
         * @return The root; none without a UI installed.
         * @facade UIRoot.Create returns root
         */
        void CreateRoot(const CallContext& context, UIScripting& ui, const ScriptUI::RootSettings* settings,
                        UIRootId* root)
        {
            UIRoots* roots = ui.Roots(context);
            if (roots == nullptr)
            {
                return;
            }
            UIRootConfig config;
            config.window = settings->window;
            config.layer = settings->layer;
            config.scaleRule = settings->scaleRule;
            config.snapToPixels = settings->snapToPixels;
            config.inputPriority = settings->inputPriority;
            config.defaultStylesheet = settings->defaultStylesheet;
            config.virtualCursor = settings->virtualCursor;
            config.hotReload = settings->hotReload;
            *root = roots->CreateRoot(std::move(config)).GetId();
        }

        /**
         * @brief The first root made, in C++ or C#, on the window's overlay at the layer: how a script finds a root the
         * game made.
         * @param window The window, as the input and window contracts name it.
         * @param layer The overlay layer.
         * @param[out] root The root found.
         * @return The root; none when no root is there.
         * @facade UIRoot.Find returns root
         */
        bool FindRoot(const CallContext& context, UIScripting& ui, WindowId window, std::int32_t layer, UIRootId* root)
        {
            UIRoots* roots = ui.Roots(context);
            if (roots == nullptr)
            {
                return false;
            }
            for (UIRoot* made : roots->GetRoots())
            {
                const UIRootConfig& config = made->GetConfig();
                if (config.target == nullptr && config.window == window && config.layer == layer)
                {
                    *root = made->GetId();
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Destroys the root and its elements: it draws no more, and its natives do nothing from now on.
         * @return Whether there was a root to destroy.
         * @facade UIRoot.Destroy
         */
        bool DestroyRoot(const CallContext& context, UIScripting& ui, const UIRootId* root)
        {
            UIRoot* found = ui.RootOf(context, *root);
            return found != nullptr && context.GetApp().GetResource<UIRoots>().DestroyRoot(*found);
        }

        /**
         * @brief The root element, which fills the target and cannot be removed.
         * @param[out] element The root element.
         * @return The root element; none for a destroyed root.
         * @facade UIRoot.GetRootElement returns element
         */
        void GetRootElement(const CallContext& context, UIScripting& ui, const UIRootId* root, ElementId* element)
        {
            if (const UIRoot* found = ui.RootOf(context, *root))
            {
                *element = found->GetRootElement();
            }
        }

        /**
         * @brief Whether the element is alive in the root: made, and not removed.
         * @facade UIRoot.IsAlive
         */
        bool IsAlive(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element)
        {
            const UIRoot* found = ui.RootOf(context, *root);
            return found != nullptr && found->IsAlive(*element);
        }

        /**
         * @brief Makes an element of `tag` under `parent` (UIRoot::Create): a registered tag's control or custom
         * element, or a plain panel for any other.
         * @param tag The element's tag, as markup names it: `panel`, `label`, `button` and the rest.
         * @param index Its place among the parent's children; negative for after the last.
         * @param[out] created The element made.
         * @return The element made; none for a stale parent.
         * @facade UIRoot.CreateElement returns created
         */
        void CreateElement(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* parent,
                           std::string_view tag, std::int32_t index, ElementId* created)
        {
            if (UIRoot* found = ui.RootWith(context, *root, *parent))
            {
                *created = found->Create(*parent, tag, IndexOf(index));
            }
        }

        /**
         * @brief Removes the element and everything under it: after its `:outro` transitions end, or at once
         * (UIRoot::Remove). Its id is stale from then on.
         * @return Whether there was an element to remove.
         * @facade UIRoot.Remove
         */
        bool Remove(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                    RemoveMode mode)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            return found != nullptr && found->Remove(*element, mode);
        }

        /**
         * @brief Moves the element, with everything under it, to be `parent`'s child (UIRoot::Reparent).
         * @param[in] parent The new parent.
         * @param index Its place among the new parent's children; negative for after the last.
         * @return Whether it moved: not for the root element, or a parent inside the element.
         * @facade UIRoot.Reparent
         */
        bool Reparent(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                      const ElementId* parent, std::int32_t index)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            return found != nullptr && found->Reparent(*element, *parent, IndexOf(index));
        }

        /**
         * @brief Moves the element among its siblings (UIRoot::Reorder).
         * @param[in] element The element moved.
         * @param index Its new place; negative for after the last.
         * @return Whether it moved.
         * @facade UIRoot.Reorder
         */
        bool Reorder(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                     std::int32_t index)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            return found != nullptr && found->Reorder(*element, IndexOf(index));
        }

        /**
         * @brief The element's parent.
         * @param[out] parent Its parent.
         * @return Its parent; none for the root element or a stale id.
         * @facade UIRoot.GetParent returns parent
         */
        void GetParent(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                       ElementId* parent)
        {
            if (const UIRoot* found = ui.RootWith(context, *root, *element))
            {
                *parent = found->GetParent(*element);
            }
        }

        /**
         * @brief Writes the element's children, in order, into `children`, as many as fit.
         * @param children Where they are written.
         * @return How many children it has, so a caller can tell a cut-short copy; 0 for a stale id.
         * @facade UIRoot.GetChildren
         */
        std::int32_t GetChildren(const CallContext& context, UIScripting& ui, const UIRootId* root,
                                 const ElementId* element, ElementId* children, std::int32_t count)
        {
            const UIRoot* found = ui.RootWith(context, *root, *element);
            return found == nullptr ? 0 : WriteIds(found->GetChildren(*element), children, count);
        }

        /**
         * @brief Writes the element's tag into `tag`, as much as fits.
         * @return The whole tag's length in chars; 0 for a stale id.
         * @facade UIRoot.GetTag
         */
        std::int32_t GetTag(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                            char16_t* tag, std::int32_t capacity)
        {
            const UIRoot* found = ui.RootWith(context, *root, *element);
            return found == nullptr ? 0 : WriteText(tag, capacity, Utf8ToUtf16(found->GetTag(*element)));
        }

        /**
         * @brief Writes the element's id string into `id`, as much as fits.
         * @return The whole id's length in chars; 0 when it has none or for a stale id.
         * @facade UIRoot.GetIdString
         */
        std::int32_t GetIdString(const CallContext& context, UIScripting& ui, const UIRootId* root,
                                 const ElementId* element, char16_t* id, std::int32_t capacity)
        {
            const UIRoot* found = ui.RootWith(context, *root, *element);
            return found == nullptr ? 0 : WriteText(id, capacity, Utf8ToUtf16(found->GetIdString(*element)));
        }

        /**
         * @brief Sets the element's id string, which `#id` selectors and FindById match.
         * @return Whether the element is alive.
         * @facade UIRoot.SetIdString
         */
        bool SetIdString(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                         std::string_view id)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            if (found != nullptr)
            {
                found->SetIdString(*element, id);
            }
            return found != nullptr;
        }

        /**
         * @brief The first element in tree order whose id string is `id`.
         * @param[out] element The element found.
         * @return The element; none when no element has the id.
         * @facade UIRoot.FindById returns element
         */
        bool FindById(const CallContext& context, UIScripting& ui, const UIRootId* root, std::string_view id,
                      ElementId* element)
        {
            const UIRoot* found = ui.RootOf(context, *root);
            if (found == nullptr)
            {
                return false;
            }
            *element = found->FindById(id);
            return element->IsValid();
        }

        /**
         * @brief Writes every element with the class `name`, in tree order, into `elements`, as many as fit.
         * @param elements Where they are written.
         * @return How many elements have the class.
         * @facade UIRoot.FindByClass
         */
        std::int32_t FindByClass(const CallContext& context, UIScripting& ui, const UIRootId* root,
                                 std::string_view name, ElementId* elements, std::int32_t count)
        {
            const UIRoot* found = ui.RootOf(context, *root);
            return found == nullptr ? 0 : WriteIds(found->FindByClass(name), elements, count);
        }

        /**
         * @brief Adds the class `name` unless the element has it.
         * @return Whether the element is alive.
         * @facade UIRoot.AddClass
         */
        bool AddClass(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                      std::string_view name)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            if (found != nullptr)
            {
                found->AddClass(*element, name);
            }
            return found != nullptr;
        }

        /**
         * @brief Removes the class `name`.
         * @return Whether the element is alive.
         * @facade UIRoot.RemoveClass
         */
        bool RemoveClass(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                         std::string_view name)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            if (found != nullptr)
            {
                found->RemoveClass(*element, name);
            }
            return found != nullptr;
        }

        /**
         * @brief Adds the class `name` when the element lacks it, and removes it when it has it.
         * @return Whether the element has the class now; false for a stale id.
         * @facade UIRoot.ToggleClass
         */
        bool ToggleClass(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                         std::string_view name)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            if (found == nullptr)
            {
                return false;
            }
            if (found->HasClass(*element, name))
            {
                found->RemoveClass(*element, name);
                return false;
            }
            found->AddClass(*element, name);
            return true;
        }

        /**
         * @brief Whether the element has the class `name`.
         * @facade UIRoot.HasClass
         */
        bool HasClass(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                      std::string_view name)
        {
            const UIRoot* found = ui.RootWith(context, *root, *element);
            return found != nullptr && found->HasClass(*element, name);
        }

        /**
         * @brief Gives the inline style property `name` the value `value`, as CSS writes it (UIRoot::SetStyleProperty):
         * `SetStyle(element, "width", "120px")`.
         * @return Whether it took the value: false, logged, for a property or value it cannot read, or a stale id.
         * @facade UIRoot.SetStyle
         */
        bool SetStyle(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                      std::string_view name, std::string_view value)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            return found != nullptr && found->SetStyleProperty(*element, name, value);
        }

        /**
         * @brief Gives the element `text`, as a label has it, laid out in its text properties (UIRoot::SetText).
         * @return Whether the element is alive.
         * @facade UIRoot.SetText
         */
        bool SetText(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                     std::string_view text)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            if (found != nullptr)
            {
                found->SetText(*element, text);
            }
            return found != nullptr;
        }

        /**
         * @brief Writes the element's text into `text`, as much as fits.
         * @return The whole text's length in chars; 0 when it has none or for a stale id.
         * @facade UIRoot.GetText
         */
        std::int32_t GetText(const CallContext& context, UIScripting& ui, const UIRootId* root,
                             const ElementId* element, char16_t* text, std::int32_t capacity)
        {
            const UIRoot* found = ui.RootWith(context, *root, *element);
            return found == nullptr ? 0 : WriteText(text, capacity, Utf8ToUtf16(found->GetText(*element)));
        }

        /**
         * @brief Gives the element's attribute `name` the value `value`, as markup writes it: a control's `min`,
         * `max`, `step`, `value`, `checked`, `disabled`, `placeholder` and the rest (Controls.hpp).
         * @return Whether it took the value: false when the element has no such attribute, could not read the value,
         * or is stale.
         * @facade UIRoot.SetAttribute
         */
        bool SetAttribute(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                          std::string_view name, std::string_view value)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            return found != nullptr && found->GetElement(*element)->SetAttribute(name, value) == AttributeResult::Set;
        }

        /**
         * @brief A control's value: a slider's or a progress bar's, or 1 for a checked checkbox and 0 for one that is
         * not.
         * @param[out] value The value.
         * @return Whether the element is one of those controls.
         * @facade UIRoot.GetValue
         */
        bool GetValue(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                      float* value)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            Element* behaviour = found == nullptr ? nullptr : found->GetElement(*element);
            if (const auto* slider = dynamic_cast<const Slider*>(behaviour))
            {
                *value = slider->GetValue();
            }
            else if (const auto* progress = dynamic_cast<const ProgressBar*>(behaviour))
            {
                *value = progress->GetValue();
            }
            else if (const auto* checkbox = dynamic_cast<const Checkbox*>(behaviour))
            {
                *value = checkbox->IsChecked() ? 1.0f : 0.0f;
            }
            else
            {
                return false;
            }
            return true;
        }

        /**
         * @brief Sets a control's value, sending no Change: a slider's (clamped to its range and stepped) or a
         * progress bar's, or a checkbox's checked state (any value but 0 checks it).
         * @return Whether the element is one of those controls.
         * @facade UIRoot.SetValue
         */
        bool SetValue(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                      float value)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            Element* behaviour = found == nullptr ? nullptr : found->GetElement(*element);
            if (auto* slider = dynamic_cast<Slider*>(behaviour))
            {
                slider->SetValue(value);
            }
            else if (auto* progress = dynamic_cast<ProgressBar*>(behaviour))
            {
                progress->SetValue(value);
            }
            else if (auto* checkbox = dynamic_cast<Checkbox*>(behaviour))
            {
                checkbox->SetChecked(value != 0.0f);
            }
            else
            {
                return false;
            }
            return true;
        }

        /**
         * @brief Writes a text entry's value into `text`, as much as fits.
         * @return The whole value's length in chars; 0 for an element that is not a text entry.
         * @facade UIRoot.GetEntryText
         */
        std::int32_t GetEntryText(const CallContext& context, UIScripting& ui, const UIRootId* root,
                                  const ElementId* element, char16_t* text, std::int32_t capacity)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            const auto* entry =
                found == nullptr ? nullptr : dynamic_cast<const TextEntry*>(found->GetElement(*element));
            return entry == nullptr ? 0 : WriteText(text, capacity, Utf8ToUtf16(entry->GetValue()));
        }

        /**
         * @brief Sets a text entry's value, sending no Change.
         * @return Whether the element is a text entry.
         * @facade UIRoot.SetEntryText
         */
        bool SetEntryText(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                          std::string_view text)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            auto* entry = found == nullptr ? nullptr : dynamic_cast<TextEntry*>(found->GetElement(*element));
            if (entry != nullptr)
            {
                entry->SetValue(text);
            }
            return entry != nullptr;
        }

        /**
         * @brief The element's rectangles as of the last frame's layout, in UI units from the root's top-left.
         * @param[out] boxes Its border, padding, content and clip boxes.
         * @return Its rectangles; empty ones for a stale id.
         * @facade UIRoot.GetBoxes returns boxes
         */
        void GetBoxes(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                      ElementBoxes* boxes)
        {
            if (const UIRoot* found = ui.RootWith(context, *root, *element))
            {
                *boxes = found->GetBoxes(*element);
            }
        }

        /**
         * @brief Makes the element focusable or not (UIRoot::SetFocusable).
         * @return Whether the element is alive.
         * @facade UIRoot.SetFocusable
         */
        bool SetFocusable(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                          bool focusable)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            if (found != nullptr)
            {
                found->SetFocusable(*element, focusable);
            }
            return found != nullptr;
        }

        /**
         * @brief Gives the element the focus (UIRoot::Focus).
         * @return Whether it took it: false for a stale or unfocusable element.
         * @facade UIRoot.Focus
         */
        bool Focus(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            return found != nullptr && found->Focus(*element);
        }

        /**
         * @brief Takes the focus from whichever of the root's elements has it.
         * @return Whether the root is there.
         * @facade UIRoot.Blur
         */
        bool Blur(const CallContext& context, UIScripting& ui, const UIRootId* root)
        {
            UIRoot* found = ui.RootOf(context, *root);
            if (found != nullptr)
            {
                found->Blur();
            }
            return found != nullptr;
        }

        /**
         * @brief The element that has the focus.
         * @param[out] element The focused element.
         * @return The focused element; none when no element of the root has it.
         * @facade UIRoot.GetFocused returns element
         */
        void GetFocused(const CallContext& context, UIScripting& ui, const UIRootId* root, ElementId* element)
        {
            if (const UIRoot* found = ui.RootOf(context, *root))
            {
                *element = found->GetFocused();
            }
        }

        /**
         * @brief Loads the markup document at `path` (relative to the asset root, or absolute) under `parent`, after
         * its children (UIRoot::LoadDocument). Its errors are logged with their files and lines; a document with errors
         * loads what it could.
         * @param path The document's path.
         * @param[out] first The first element at the document's top level; none when none loaded.
         * @return Whether it loaded with no errors.
         * @facade UIRoot.LoadDocument
         */
        bool LoadDocument(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* parent,
                          std::string_view path, ElementId* first)
        {
            UIRoot* found = ui.RootWith(context, *root, *parent);
            if (found == nullptr)
            {
                return false;
            }
            const MarkupLoad load = found->LoadDocument(*parent, path);
            *first = load.element;
            return load.errors.empty();
        }

        /**
         * @brief Loads the stylesheet at `path` (relative to the asset root, or absolute) and adds it after the root's
         * other sheets (UIRoot::LoadStylesheet). What it refused is logged with its file and line.
         * @param path The stylesheet's path.
         * @return Whether the file was there and was added.
         * @facade UIRoot.LoadStylesheet
         */
        bool LoadStylesheet(const CallContext& context, UIScripting& ui, const UIRootId* root, std::string_view path)
        {
            UIRoot* found = ui.RootOf(context, *root);
            return found != nullptr && found->LoadStylesheet(path).IsValid();
        }

        /**
         * @brief A painter for the element, inside a Fenrir.UI.Paint stage system (UIRoot::PaintInto): what it draws
         * through the Painter facade is painted after the element's own boxes and before its children, in the root's
         * UI units (GetBoxes's), under the element's transform and clips. Calls for one element in one stage give one
         * painter; it ends as the stage ends, and a later call on it draws nothing.
         * @param[out] painter The painter.
         * @return The painter; none outside the stage, for a stale id, or on a frame the root does not paint.
         * @facade UIRoot.PaintInto returns painter
         */
        void PaintInto(const CallContext& context, UIScripting& ui, const UIRootId* root, const ElementId* element,
                       ScriptPainter::PainterId* painter)
        {
            UIRoot* found = ui.RootWith(context, *root, *element);
            if (found == nullptr)
            {
                return;
            }
            const NativeGuard guard(context, ui.warnings);
            if (!ui.painting)
            {
                guard.Warn(std::string(UIScripting::NoPainterExtension));
                return;
            }
            for (const UIScripting::Lent& given : ui.lent)
            {
                if (given.root == *root && given.element == *element)
                {
                    *painter = given.painter;
                    return;
                }
            }
            Painter* given = guard.Require(found->PaintInto(*element), std::string(UIScripting::NotPainting));
            if (given == nullptr)
            {
                return;
            }
            *painter = ui.painting->Lend(context.GetApp(), *given);
            ui.lent.push_back({*root, *element, *painter});
        }
    } // namespace UINatives

    namespace
    {
        // Places the loaded script's paint stage groups in UIRoots' paint stage, each at its order, and after them all
        // the stage's end, which takes back the painters lent and plays back the stage changes.
        void PlaceStages(const std::weak_ptr<UIScripting>& weak, App& app, std::vector<StageGroup> groups,
                         StageChanges changes)
        {
            const std::shared_ptr<UIScripting> state = weak.lock();
            UIRoots* roots = app.TryGetResource<UIRoots>();
            if (!state || roots == nullptr)
            {
                return;
            }
            state->changes = std::move(changes);
            for (StageGroup& group : groups)
            {
                state->placed.push_back(roots->AddPaintStage(group.Order(), [group] { group.Run(); }));
            }
            state->placed.push_back(roots->AddPaintStage(std::numeric_limits<std::int32_t>::max(), [weak] {
                if (const std::shared_ptr<UIScripting> running = weak.lock())
                {
                    running->EndStage();
                    if (running->changes)
                    {
                        running->changes->PlayBack();
                    }
                }
            }));
        }

        void RemoveStages(const std::weak_ptr<UIScripting>& weak, App& app)
        {
            const std::shared_ptr<UIScripting> state = weak.lock();
            if (!state)
            {
                return;
            }
            if (UIRoots* roots = app.TryGetResource<UIRoots>())
            {
                for (const PaintStageHandle handle : state->placed)
                {
                    (void)roots->RemovePaintStage(handle);
                }
            }
            state->placed.clear();
            state->EndStage();
            state->changes.reset();
        }
    } // namespace

    Fenrir::ScriptingExtension UI::ScriptingExtension(const Fenrir::ScriptingExtension& painter)
    {
        auto state = std::make_shared<UIScripting>();
        state->painting = painter.GetState<PainterScripting>();
        const std::weak_ptr<UIScripting> weak = state;
        Fenrir::ScriptingExtension extension = Generated::MakeUIExtension(std::move(state));
        extension.placeStages = [weak](App& app, std::vector<StageGroup> groups, StageChanges changes) {
            PlaceStages(weak, app, std::move(groups), std::move(changes));
        };
        extension.removeStages = [weak](App& app) { RemoveStages(weak, app); };
        return extension;
    }
} // namespace Fenrir
