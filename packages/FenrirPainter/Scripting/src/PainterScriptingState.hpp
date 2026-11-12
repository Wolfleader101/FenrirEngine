#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Path.hpp"
#include "FenrirPainterScripting/ScriptPainter.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"
#include "FenrirTextScripting/TextScriptingState.hpp"

namespace Fenrir
{
    /**
     * @brief The painter extension's state, which every painter native function is given second: the overlay painters
     * of the running C# system and the paths scripts made, by id, and the text extension's state, whose paragraphs
     * DrawText draws. Made by Painting::ScriptingExtension and kept by the Scripting given the extension. In a header
     * of its own because the generated registration declares the natives, which name it. The text the natives warn with
     * is named here, so the package's rules read in one place and its rules tests name them.
     */
    struct PainterScripting
    {
        /** @brief Warned when the app has no Overlay resource (the renderer is not installed): no painter is given. */
        static constexpr std::string_view NoOverlay =
            "the app has no Overlay (the renderer is not installed), so it gives no painter";
        /** @brief Warned when the app's asset store has no builtin:painter: no painter is given. */
        static constexpr std::string_view NoPainterShader =
            "the app's asset store has no builtin:painter (Painting::Install was not called), so it gives no painter";
        /** @brief Warned for a painter that ended or was never given: the native does nothing. */
        static constexpr std::string_view NoPainter =
            "the painter ended (its changes played back, or its frame is over) or was never given, so it does nothing";
        /** @brief Warned when the extension was made without the text extension's state: no text is drawn. */
        static constexpr std::string_view NoTextExtension =
            "the painter extension was made without the text extension, so it draws no text";
        /** @brief Warned for a path that was destroyed or never made: the native does nothing. */
        static constexpr std::string_view NoPath = "the path was destroyed or never made, so it does nothing";

        /**
         * @brief An overlay painter: the window and layer it paints, the draw list it records into, and the painter
         * over the list. Kept behind a pointer, since the painter refers to the list.
         */
        struct Recording
        {
            WindowId window;
            std::int32_t layer = 0;
            /** @brief The frame it was given in (Time::frameCount): it paints nothing in a later one. */
            std::uint64_t frame = 0;
            DrawList2D list;
            std::optional<Painter> painter;
        };

        /**
         * @brief The painter `painter` names, or null, warning once under the calling native's name, when it ended or
         * was given in an earlier frame.
         */
        Painter* PainterOf(const CallContext& context, const ScriptPainter::PainterId& painter)
        {
            const NativeGuard guard(context, warnings);
            const auto found = painters.find(painter.value);
            const bool current = found != painters.end() && found->second->frame == FrameOf(context.GetApp());
            return guard.Require(current ? &*found->second->painter : nullptr, std::string(NoPainter));
        }

        /** @brief The frame `app` is running (Time::frameCount), which a painter is given in. */
        static std::uint64_t FrameOf(const App& app)
        {
            return app.GetTime().frameCount;
        }

        /**
         * @brief Forgets, unsubmitted, every painter given in a frame before `frame`: one whose submission never played
         * back, as when its system threw and its changes were discarded, so its window and layer get a new painter.
         */
        void DropOtherFrames(std::uint64_t frame)
        {
            for (auto it = painters.begin(); it != painters.end();)
            {
                if (it->second->frame == frame)
                {
                    ++it;
                    continue;
                }
                overlays.erase({it->second->window.value, it->second->layer});
                it = painters.erase(it);
            }
        }

        /**
         * @brief The path `path` names, or null, warning once under the calling native's name, when it was destroyed
         * or never made.
         */
        Path* FindPath(const CallContext& context, const ScriptPainter::Path2D& path)
        {
            const NativeGuard guard(context, warnings);
            const auto found = paths.find(path.id);
            return guard.Require(found == paths.end() ? nullptr : &found->second, std::string(NoPath));
        }

        /**
         * @brief Ends the overlay painter `id`: its list is submitted to `app`'s Overlay at its window and layer, and
         * it is forgotten. Played back with the changes of the C# code that was given it: when its system returns, or
         * when a hosted stage's group of systems ends.
         */
        void Submit(App& app, std::uint64_t id)
        {
            const auto found = painters.find(id);
            if (found == painters.end())
            {
                return;
            }
            const std::unique_ptr<Recording> recording = std::move(found->second);
            painters.erase(found);
            overlays.erase({recording->window.value, recording->layer});
            recording->painter.reset();
            if (Overlay* overlay = app.TryGetResource<Overlay>())
            {
                overlay->Submit(recording->window, recording->layer, std::move(recording->list));
            }
        }

        /**
         * @brief The paragraph `paragraph` names in the text extension, or null, warning once under the calling
         * native's name when the text extension does not have it (TextScripting::NoParagraph) or was not given.
         */
        Paragraph* FindParagraph(const CallContext& context, const ScriptText::Paragraph& paragraph)
        {
            if (text == nullptr)
            {
                const NativeGuard guard(context, warnings);
                guard.Warn(std::string(NoTextExtension));
                return nullptr;
            }
            return text->Find(context, paragraph);
        }

        /** @brief The overlay painters given and not yet submitted, by id. */
        std::unordered_map<std::uint64_t, std::unique_ptr<Recording>> painters;
        /** @brief Which of them paints each window and layer, by the window's value and the layer. */
        std::map<std::pair<std::uint32_t, std::int32_t>, std::uint64_t> overlays;
        /** @brief The paths scripts made and have not destroyed, by id. Node-based, so each stays where it is. */
        std::unordered_map<std::uint64_t, Path> paths;
        /** @brief The ids the next painter and path get: ids are never handed out again. */
        std::uint64_t nextPainter = 1;
        std::uint64_t nextPath = 1;
        /** @brief The text extension's state, whose paragraphs DrawText draws (TextScripting::Find). */
        std::shared_ptr<TextScripting> text;
        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"painter scripting"};
    };
} // namespace Fenrir
