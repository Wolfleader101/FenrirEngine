#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirPainterScripting/PainterScriptingState.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"
#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "FenrirUI/UIRoot.hpp"

namespace Fenrir
{
    /**
     * @brief The UI extension's state, which every UI native function is given second: the painter extension's state,
     * which it lends the paint stage's painters through, the painters it lent this stage, and what it placed in the
     * paint stage for the loaded script. Made by UI::ScriptingExtension and kept by the Scripting given the extension.
     * In a header of its own because the generated registration declares the natives, which name it. The text the
     * natives warn with is named here, so the package's rules read in one place and its rules tests name them.
     */
    struct UIScripting
    {
        /** @brief Warned when the app has no UI (UI::Install was not called): the native does nothing. */
        static constexpr std::string_view NoUI = "the app has no UI (UI::Install was not called), so it does nothing";
        /** @brief Warned for a root that was destroyed or never made: the native does nothing. */
        static constexpr std::string_view NoRoot = "the UI root was destroyed or never made, so it does nothing";
        /** @brief Warned for an element that was removed or never made: the native does nothing. */
        static constexpr std::string_view NoElement = "the element was removed or never made, so it does nothing";
        /** @brief Warned when PaintInto is called outside the paint stage: it gives no painter. */
        static constexpr std::string_view NotPainting =
            "it was called outside a Fenrir.UI.Paint stage system, or on a frame the root does not paint, so it gives "
            "no painter";
        /** @brief Warned when the extension was made without the painter extension's state: it gives no painter. */
        static constexpr std::string_view NoPainterExtension =
            "the UI extension was made without the painter extension, so it gives no painter";
        /** @brief Warned when an image root is asked for in an app with no asset store: it makes no root. */
        static constexpr std::string_view NoAssetStore =
            "the app has no asset store (Assets::Install was not called), so it makes no image root";
        /** @brief Warned when a world root is asked for on an entity without a WorldQuad2D: it makes no root. */
        static constexpr std::string_view NoWorldQuad = "the entity has no WorldQuad2D, so it makes no world root";
        /** @brief Warned for a pointer ray given to a root not on a world quad: the native does nothing. */
        static constexpr std::string_view TakesNoRay =
            "the root takes no pointer ray (it is not on a world quad), so it does nothing";

        /** @brief A painter lent for one element in the running paint stage. */
        struct Lent
        {
            UIRootId root;
            ElementId element;
            ScriptPainter::PainterId painter;
        };

        /** @brief The app's UIRoots, or null, warning once under the calling native's name when it has no UI. */
        UIRoots* Roots(const CallContext& context)
        {
            const NativeGuard guard(context, warnings);
            return guard.Require(context.GetApp().TryGetResource<UIRoots>(), std::string(NoUI));
        }

        /** @brief The root `root` names, or null, warning once under the calling native's name when there is none. */
        UIRoot* RootOf(const CallContext& context, const UIRootId& root)
        {
            UIRoots* roots = Roots(context);
            if (roots == nullptr)
            {
                return nullptr;
            }
            const NativeGuard guard(context, warnings);
            for (UIRoot* made : roots->GetRoots())
            {
                if (made->GetId() == root)
                {
                    return made;
                }
            }
            guard.Warn(std::string(NoRoot));
            return nullptr;
        }

        /**
         * @brief The root `root` names when it takes a pointer ray (it is on a world quad), or null, warning once
         * under the calling native's name when there is no root or it takes no ray.
         */
        UIRoot* RayRootOf(const CallContext& context, const UIRootId& root)
        {
            UIRoot* found = RootOf(context, root);
            if (found == nullptr)
            {
                return nullptr;
            }
            const UITarget* target = found->GetConfig().target.get();
            const NativeGuard guard(context, warnings);
            return guard.Require(target != nullptr && target->TakesPointerRay() ? found : nullptr,
                                 std::string(TakesNoRay));
        }

        /**
         * @brief The root `root` names when `element` is alive in it, or null, warning once under the calling native's
         * name when the root or the element is not there.
         */
        UIRoot* RootWith(const CallContext& context, const UIRootId& root, const ElementId& element)
        {
            UIRoot* found = RootOf(context, root);
            if (found == nullptr)
            {
                return nullptr;
            }
            const NativeGuard guard(context, warnings);
            return guard.Require(found->IsAlive(element) ? found : nullptr, std::string(NoElement));
        }

        /**
         * @brief Destroys every image and world root the released script generation made that is still there: each
         * root holds its target, so the image is unloaded and the world target ends with it.
         */
        void EndMadeRoots(App& app)
        {
            if (UIRoots* roots = app.TryGetResource<UIRoots>())
            {
                for (UIRoot* root : roots->GetRoots())
                {
                    if (std::find(made.begin(), made.end(), root->GetId()) != made.end())
                    {
                        (void)roots->DestroyRoot(*root);
                    }
                }
            }
            made.clear();
        }

        /** @brief Takes back every painter lent in the stage that is ending: their ids paint nothing from now on. */
        void EndStage()
        {
            if (painting)
            {
                for (const Lent& given : lent)
                {
                    painting->EndLent(given.painter);
                }
            }
            lent.clear();
        }

        /** @brief The painter extension's state, which lends C# the paint stage's painters. */
        std::shared_ptr<PainterScripting> painting;
        /** @brief The painters lent in the running paint stage, taken back as it ends. */
        std::vector<Lent> lent;
        /** @brief What placeStages put in the build stage for the loaded script: its groups and the stage's end. */
        std::vector<BuildStageHandle> placedBuild;
        /** @brief What placeStages put in the paint stage for the loaded script: its groups and the stage's end. */
        std::vector<PaintStageHandle> placedPaint;
        /**
         * @brief The image and world roots the loaded script generation made (CreateImageRoot, CreateWorldRoot), which
         * its release destroys. Its window roots are the game's to keep, as before.
         */
        std::vector<UIRootId> made;
        /** @brief The loaded script's stage changes, played back as each stage ends. */
        std::optional<StageChanges> changes;
        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"UI scripting"};
    };
} // namespace Fenrir
