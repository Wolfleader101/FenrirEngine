#pragma once

#include <bitset>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "FenrirUI/Style.hpp"
#include "FenrirUI/Stylesheet.hpp"
#include "StyleValues.hpp"

namespace Fenrir::Detail
{
    /** @brief One keyframe of a `@keyframes` rule, read for animations: where it sits (0 to 1) and what it gives. */
    struct CompiledKeyframe
    {
        float offset = 0.0f;
        /** @brief Its properties; an `animation-timing-function` among them eases the interval it starts. */
        StyleDeclarations values;
    };

    /** @brief A `@keyframes` rule read for animations: its keyframes by offset and the properties they animate. */
    struct CompiledKeyframes
    {
        std::string name;
        std::vector<CompiledKeyframe> keyframes;
        std::bitset<StylePropertyCount> properties;
    };

    /** @brief A root's `@keyframes` by name: of rules sharing a name, the last one of the last sheet, as CSS. */
    using KeyframesByName = std::unordered_map<std::string, std::shared_ptr<const CompiledKeyframes>>;

    /** @brief A property's change in flight from the value it showed to its new cascaded value. */
    struct RunningTransition
    {
        StyleProperty property = StyleProperty::Count;
        /** @brief Seconds since it started. */
        float elapsed = 0.0f;
        float duration = 0.0f;
        float delay = 0.0f;
        TimingFunction timing;
        /** @brief Its two values, in `property`'s field; their other fields are unused. */
        ResolvedStyle from;
        ResolvedStyle to;
    };

    /** @brief One of an element's `animation-name`s playing its keyframes. */
    struct RunningAnimation
    {
        std::string name;
        /** @brief Its place in the animation lists, which gives its duration, delay and the rest. */
        std::size_t index = 0;
        std::shared_ptr<const CompiledKeyframes> keyframes;
        /** @brief Seconds since it started. */
        float elapsed = 0.0f;
        /** @brief Its last iteration has ended: it changes no more, and holds its end values only with a fill. */
        bool finished = false;
    };

    /** @brief An element's transitions and animations. */
    struct ElementMotion
    {
        std::vector<RunningTransition> transitions;
        std::vector<RunningAnimation> animations;

        /** @brief Whether anything in it changes with time: a transition, or an animation not yet finished. */
        [[nodiscard]] bool IsMoving() const noexcept;
    };

    /**
     * @brief Reads a sheet's `@keyframes` for animations; a declaration that cannot be read is skipped and said in
     * `skipped` (one line each, with the sheet's line), as the sheet's rules are.
     */
    [[nodiscard]] std::vector<std::shared_ptr<const CompiledKeyframes>> CompileKeyframes(
        const Stylesheet& sheet, StyleParseContext& context, std::vector<std::string>& skipped);

    /**
     * @brief After an element's cascaded style changed from `before` to `after`, starts a transition for each property
     * that changed and that `after`'s transition lists name with a duration and delay that run, from the value it shows
     * now (a running transition's current value, so a change mid-transition continues from there), and drops those a
     * change has made moot.
     */
    void StartTransitions(ElementMotion& motion, const ResolvedStyle& before, const ResolvedStyle& after);

    /**
     * @brief Brings an element's animations in line with `style`'s `animation-name` list: a name it already plays
     * keeps its time, a new one starts, a name gone stops, and one with no `@keyframes` in `keyframes` plays nothing.
     */
    void ReconcileAnimations(ElementMotion& motion, const ResolvedStyle& style, const KeyframesByName& keyframes);

    /**
     * @brief Moves an element's transitions and animations on by `seconds`, under its cascaded style `style`: ended
     * transitions are dropped and ended animations finish. True when what it shows may have changed.
     */
    bool AdvanceMotion(ElementMotion& motion, const ResolvedStyle& style, float seconds);

    /**
     * @brief Writes what an element's animations and then its transitions show over `style`, which holds its cascaded
     * values on the way in.
     */
    void ApplyMotion(const ElementMotion& motion, ResolvedStyle& style);
} // namespace Fenrir::Detail
