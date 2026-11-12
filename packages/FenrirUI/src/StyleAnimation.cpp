#include "StyleAnimation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <variant>

namespace Fenrir::Detail
{
    namespace
    {
        /** @name Comparing values
         * What a property's value equals, for the types the style holds that have no `==` of their own.
         * @{
         */
        template <typename T>
        bool Same(const T& a, const T& b)
        {
            return a == b;
        }

        bool Same(const Colour& a, const Colour& b)
        {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }

        bool Same(const Shadow& a, const Shadow& b)
        {
            return a.offset == b.offset && a.blur == b.blur && a.spread == b.spread && Same(a.colour, b.colour) &&
                   a.inset == b.inset;
        }

        bool Same(const TransformStep& a, const TransformStep& b)
        {
            return a.kind == b.kind && a.x == b.x && a.y == b.y && a.factors == b.factors && a.radians == b.radians;
        }

        bool Same(const GradientStop& a, const GradientStop& b)
        {
            return a.offset == b.offset && Same(a.colour, b.colour);
        }

        template <typename T>
        bool Same(const std::vector<T>& a, const std::vector<T>& b)
        {
            return a.size() == b.size() &&
                   std::equal(a.begin(), a.end(), b.begin(), [](const T& x, const T& y) { return Same(x, y); });
        }

        bool Same(const BackgroundImage& a, const BackgroundImage& b)
        {
            if (a.index() != b.index())
            {
                return false;
            }
            if (const auto* image = std::get_if<AssetHandle<Image>>(&a))
            {
                return *image == std::get<AssetHandle<Image>>(b);
            }
            if (const auto* linear = std::get_if<StyleLinearGradient>(&a))
            {
                const auto& other = std::get<StyleLinearGradient>(b);
                return linear->degrees == other.degrees && Same(linear->stops, other.stops);
            }
            if (const auto* radial = std::get_if<StyleRadialGradient>(&a))
            {
                return Same(radial->stops, std::get<StyleRadialGradient>(b).stops);
            }
            return true;
        }
        /** @} */

        /** @name Mixing values
         * The value `t` of the way from `a` to `b` (0 gives `a`, 1 gives `b`, and an overshooting timing function goes
         * past them). What cannot be mixed is discrete: it shows `a` until `t` reaches 1.
         * @{
         */
        template <typename T>
        T Mix(const T& a, const T& b, float t)
        {
            return t >= 1.0f ? b : a;
        }

        float Mix(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        std::int32_t Mix(std::int32_t a, std::int32_t b, float t)
        {
            return static_cast<std::int32_t>(std::lround(Mix(static_cast<float>(a), static_cast<float>(b), t)));
        }

        Math::Vec2 Mix(const Math::Vec2& a, const Math::Vec2& b, float t)
        {
            return a + (b - a) * t;
        }

        Length Mix(const Length& a, const Length& b, float t)
        {
            // px and % do not mix without calc(), which the style has not; nor does auto.
            if (a.unit != b.unit || a.unit == LengthUnit::Auto)
            {
                return t >= 1.0f ? b : a;
            }
            return {Mix(a.value, b.value, t), a.unit};
        }

        // The sRGB transfer function (IEC 61966-2-1), and its inverse, on either side of zero.
        float Encode(float linear)
        {
            const float magnitude = std::abs(linear);
            const float encoded =
                magnitude <= 0.0031308f ? magnitude * 12.92f : 1.055f * std::pow(magnitude, 1.0f / 2.4f) - 0.055f;
            return std::copysign(encoded, linear);
        }

        float Decode(float encoded)
        {
            const float magnitude = std::abs(encoded);
            const float linear =
                magnitude <= 0.04045f ? magnitude / 12.92f : std::pow((magnitude + 0.055f) / 1.055f, 2.4f);
            return std::copysign(linear, encoded);
        }

        Colour Mix(const Colour& a, const Colour& b, float t)
        {
            if (t <= 0.0f)
            {
                return a;
            }
            if (t >= 1.0f)
            {
                return b;
            }
            // As CSS: in sRGB's encoded values, with alpha premultiplied, so a fade from transparent does not pass
            // through the transparent colour's hue.
            const float alpha = std::clamp(Mix(a.a, b.a, t), 0.0f, 1.0f);
            if (alpha <= 0.0f)
            {
                return {0.0f, 0.0f, 0.0f, 0.0f};
            }
            const auto channel = [&](float x, float y) {
                return Decode(Mix(Encode(x) * a.a, Encode(y) * b.a, t) / alpha);
            };
            return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), alpha};
        }

        Shadow Mix(const Shadow& a, const Shadow& b, float t)
        {
            Shadow out;
            out.offset = Mix(a.offset, b.offset, t);
            out.blur = std::max(Mix(a.blur, b.blur, t), 0.0f);
            out.spread = Mix(a.spread, b.spread, t);
            out.colour = Mix(a.colour, b.colour, t);
            out.inset = a.inset;
            return out;
        }

        std::vector<Shadow> Mix(const std::vector<Shadow>& a, const std::vector<Shadow>& b, float t)
        {
            // As CSS: the shorter list is padded with transparent shadows of no size; an inset against an outset
            // shadow does not mix.
            const Shadow none{{0.0f, 0.0f}, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f, 0.0f}, false};
            std::vector<Shadow> out;
            for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i)
            {
                Shadow from = i < a.size() ? a[i] : none;
                Shadow to = i < b.size() ? b[i] : none;
                if (i >= a.size())
                {
                    from.inset = to.inset;
                }
                if (i >= b.size())
                {
                    to.inset = from.inset;
                }
                if (from.inset != to.inset)
                {
                    return t >= 1.0f ? b : a;
                }
                out.push_back(Mix(from, to, t));
            }
            return out;
        }

        /** @brief The step of `kind` that changes nothing, with lengths in `unit`. */
        TransformStep Identity(TransformStep::Kind kind, LengthUnit unit)
        {
            switch (kind)
            {
            case TransformStep::Kind::Translate:
                return TransformStep::Translate({0.0f, unit}, {0.0f, unit});
            case TransformStep::Kind::Scale:
                return TransformStep::Scale({1.0f, 1.0f});
            case TransformStep::Kind::Rotate:
                break;
            }
            return TransformStep::Rotate(0.0f);
        }

        std::vector<TransformStep> Mix(const std::vector<TransformStep>& a, const std::vector<TransformStep>& b,
                                       float t)
        {
            // Step by step when the lists' functions match (the shorter padded with ones that change nothing), as
            // CSS does before it falls back to matrices; lists that do not match are discrete.
            std::vector<TransformStep> out;
            for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i)
            {
                const TransformStep& from = i < a.size() ? a[i] : Identity(b[i].kind, b[i].x.unit);
                const TransformStep& to = i < b.size() ? b[i] : Identity(a[i].kind, a[i].x.unit);
                if (from.kind != to.kind || (from.kind == TransformStep::Kind::Translate &&
                                             (from.x.unit != to.x.unit || from.y.unit != to.y.unit)))
                {
                    return t >= 1.0f ? b : a;
                }
                TransformStep step = from;
                step.x = Mix(from.x, to.x, t);
                step.y = Mix(from.y, to.y, t);
                step.factors = Mix(from.factors, to.factors, t);
                step.radians = Mix(from.radians, to.radians, t);
                out.push_back(step);
            }
            return out;
        }
        /** @} */

        void CopyField(StyleProperty property, const ResolvedStyle& from, ResolvedStyle& to)
        {
            switch (property)
            {
#define FENRIR_UI_COPY_FIELD(Enum, Name, Field, Parse) \
    case StyleProperty::Enum:                          \
        to.Field = from.Field;                         \
        break;
                FENRIR_UI_STYLE_PROPERTIES(FENRIR_UI_COPY_FIELD)
#undef FENRIR_UI_COPY_FIELD
            case StyleProperty::Count:
                break;
            }
        }

        bool SameField(StyleProperty property, const ResolvedStyle& a, const ResolvedStyle& b)
        {
            switch (property)
            {
#define FENRIR_UI_SAME_FIELD(Enum, Name, Field, Parse) \
    case StyleProperty::Enum:                          \
        return Same(a.Field, b.Field);
                FENRIR_UI_STYLE_PROPERTIES(FENRIR_UI_SAME_FIELD)
#undef FENRIR_UI_SAME_FIELD
            case StyleProperty::Count:
                break;
            }
            return true;
        }

        void MixField(StyleProperty property, const ResolvedStyle& a, const ResolvedStyle& b, float t,
                      ResolvedStyle& out)
        {
            switch (property)
            {
#define FENRIR_UI_MIX_FIELD(Enum, Name, Field, Parse) \
    case StyleProperty::Enum:                         \
        out.Field = Mix(a.Field, b.Field, t);         \
        break;
                FENRIR_UI_STYLE_PROPERTIES(FENRIR_UI_MIX_FIELD)
#undef FENRIR_UI_MIX_FIELD
            case StyleProperty::Count:
                break;
            }
            if (property == StyleProperty::Opacity)
            {
                out.opacity = std::clamp(out.opacity, 0.0f, 1.0f);
            }
        }

        /** @brief Whether a transition or an animation can move `property`: every one but their own lists. */
        bool IsAnimatable(StyleProperty property) noexcept
        {
            return property < StyleProperty::TransitionProperty && property != StyleProperty::Count;
        }

        template <typename T>
        T At(const std::vector<T>& list, std::size_t index, T initial)
        {
            return list.empty() ? initial : list[index % list.size()];
        }

        /** @brief The value `transition` shows now, written over `out`. */
        void ApplyTransition(const RunningTransition& transition, ResolvedStyle& out)
        {
            float progress = 0.0f;
            if (transition.duration > 0.0f)
            {
                progress = std::clamp((transition.elapsed - transition.delay) / transition.duration, 0.0f, 1.0f);
            }
            else
            {
                progress = transition.elapsed >= transition.delay ? 1.0f : 0.0f;
            }
            MixField(transition.property, transition.from, transition.to, transition.timing.Apply(progress), out);
        }

        /** @brief What one of an element's animations is given by its lists. */
        struct AnimationParams
        {
            float duration = 0.0f;
            float delay = 0.0f;
            float count = 1.0f;
            TimingFunction timing;
            AnimationDirection direction = AnimationDirection::Normal;
            AnimationFillMode fill = AnimationFillMode::None;
        };

        AnimationParams ParamsOf(const ResolvedStyle& style, std::size_t index)
        {
            AnimationParams params;
            params.duration = At(style.animationDuration, index, 0.0f);
            params.delay = At(style.animationDelay, index, 0.0f);
            params.count = At(style.animationIterationCount, index, 1.0f);
            params.timing = At(style.animationTimingFunction, index, TimingFunction::Ease());
            params.direction = At(style.animationDirection, index, AnimationDirection::Normal);
            params.fill = At(style.animationFillMode, index, AnimationFillMode::None);
            return params;
        }

        /** @brief Seconds its iterations take in all: infinity for infinite ones that last. */
        float ActiveDuration(const AnimationParams& params)
        {
            return params.duration > 0.0f && params.count > 0.0f ? params.duration * params.count : 0.0f;
        }

        bool HasEnded(const AnimationParams& params, float elapsed)
        {
            return elapsed - params.delay >= ActiveDuration(params);
        }

        /**
         * @brief Where in its keyframes (0 to 1) the animation is at `elapsed`, its direction applied; nothing when it
         * shows nothing, in its delay or after its end without a fill that holds there. CSS Animations 1's model.
         */
        std::optional<float> KeyframeProgress(const AnimationParams& params, float elapsed)
        {
            const float active = elapsed - params.delay;
            float iteration = 0.0f;
            float progress = 0.0f;
            if (active < 0.0f)
            {
                if (params.fill != AnimationFillMode::Backwards && params.fill != AnimationFillMode::Both)
                {
                    return std::nullopt;
                }
            }
            else if (HasEnded(params, elapsed))
            {
                if (params.fill != AnimationFillMode::Forwards && params.fill != AnimationFillMode::Both)
                {
                    return std::nullopt;
                }
                // It ends where its last iteration does: at the end of a whole one, part way through a fraction.
                iteration = std::floor(params.count);
                progress = params.count - iteration;
                if (progress == 0.0f && params.count > 0.0f)
                {
                    iteration -= 1.0f;
                    progress = 1.0f;
                }
            }
            else
            {
                const float overall = active / params.duration;
                iteration = std::floor(overall);
                progress = overall - iteration;
            }
            const bool odd = std::fmod(iteration, 2.0f) == 1.0f;
            const bool reversed = params.direction == AnimationDirection::Reverse ||
                                  (params.direction == AnimationDirection::Alternate && odd) ||
                                  (params.direction == AnimationDirection::AlternateReverse && !odd);
            return reversed ? 1.0f - progress : progress;
        }

        /**
         * @brief Writes `property`'s value at `progress` through `keyframes` over `out`: between the keyframes that
         * give it on either side, with `underlying` (the cascaded value) at 0 and 1 where none does, eased by the
         * starting keyframe's timing function, else `timing`.
         */
        void SampleKeyframes(const CompiledKeyframes& keyframes, StyleProperty property, float progress,
                             const TimingFunction& timing, const ResolvedStyle& underlying, ResolvedStyle& out)
        {
            struct Point
            {
                float offset = 0.0f;
                const ResolvedStyle* values = nullptr;
                const TimingFunction* timing = nullptr;
            };
            std::vector<Point> points;
            for (const CompiledKeyframe& keyframe : keyframes.keyframes)
            {
                if (keyframe.values.IsSet(property))
                {
                    const std::vector<TimingFunction>& own = keyframe.values.Values().animationTimingFunction;
                    const bool eases = keyframe.values.IsSet(StyleProperty::AnimationTimingFunction) && !own.empty();
                    points.push_back({keyframe.offset, &keyframe.values.Values(), eases ? &own.front() : nullptr});
                }
            }
            if (points.empty() || points.front().offset > 0.0f)
            {
                points.insert(points.begin(), Point{0.0f, &underlying, nullptr});
            }
            if (points.back().offset < 1.0f)
            {
                points.push_back({1.0f, &underlying, nullptr});
            }
            std::size_t at = 0;
            while (at + 2 < points.size() && progress >= points[at + 1].offset)
            {
                ++at;
            }
            const Point& from = points[at];
            const Point& to = points[std::min(at + 1, points.size() - 1)];
            const float span = to.offset - from.offset;
            const float local = span > 0.0f ? std::clamp((progress - from.offset) / span, 0.0f, 1.0f) : 1.0f;
            const TimingFunction& ease = from.timing != nullptr ? *from.timing : timing;
            MixField(property, *from.values, *to.values, ease.Apply(local), out);
        }
    } // namespace

    bool ElementMotion::IsMoving() const noexcept
    {
        return !transitions.empty() ||
               std::any_of(animations.begin(), animations.end(), [](const RunningAnimation& a) { return !a.finished; });
    }

    std::vector<std::shared_ptr<const CompiledKeyframes>> CompileKeyframes(const Stylesheet& sheet,
                                                                           StyleParseContext& context,
                                                                           std::vector<std::string>& skipped)
    {
        std::vector<std::shared_ptr<const CompiledKeyframes>> compiled;
        for (const StyleKeyframes& rule : sheet.keyframes)
        {
            auto keyframes = std::make_shared<CompiledKeyframes>();
            keyframes->name = rule.name;
            for (const StyleKeyframe& block : rule.keyframes)
            {
                StyleDeclarations values;
                for (const StyleDeclaration& declaration : block.declarations)
                {
                    context.error.clear();
                    if (declaration.name.starts_with("--") || ContainsVar(declaration.value))
                    {
                        context.error = "custom properties and var() are not read in keyframes";
                    }
                    else if (ParseStyleDeclaration(declaration.name, declaration.value, values, context))
                    {
                        continue;
                    }
                    skipped.push_back(std::to_string(declaration.line) + ": the declaration '" + declaration.name +
                                      ": " + declaration.value + "' in @keyframes " + rule.name +
                                      " was skipped: " + context.error);
                }
                // As CSS, a keyframe's transition and animation properties are ignored, but for the timing function
                // that eases the interval it starts.
                for (auto p = static_cast<std::size_t>(StyleProperty::TransitionProperty); p < StylePropertyCount; ++p)
                {
                    if (static_cast<StyleProperty>(p) != StyleProperty::AnimationTimingFunction)
                    {
                        values.Unset(static_cast<StyleProperty>(p));
                    }
                }
                for (const float offset : block.offsets)
                {
                    keyframes->keyframes.push_back({offset, values});
                }
            }
            std::stable_sort(keyframes->keyframes.begin(), keyframes->keyframes.end(),
                             [](const CompiledKeyframe& a, const CompiledKeyframe& b) { return a.offset < b.offset; });
            for (const CompiledKeyframe& keyframe : keyframes->keyframes)
            {
                for (std::size_t p = 0; p < StylePropertyCount; ++p)
                {
                    if (IsAnimatable(static_cast<StyleProperty>(p)) &&
                        keyframe.values.IsSet(static_cast<StyleProperty>(p)))
                    {
                        keyframes->properties.set(p);
                    }
                }
            }
            compiled.push_back(std::move(keyframes));
        }
        return compiled;
    }

    void StartTransitions(ElementMotion& motion, const ResolvedStyle& before, const ResolvedStyle& after)
    {
        // Which entry of the transition lists each property takes: the last that names it, as CSS.
        constexpr std::size_t none = static_cast<std::size_t>(-1);
        std::array<std::size_t, StylePropertyCount> entry;
        entry.fill(none);
        const std::vector<std::string> all{"all"};
        const std::vector<std::string>& names = after.transitionProperty.empty() ? all : after.transitionProperty;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (names[i] == "all")
            {
                for (std::size_t p = 0; p < StylePropertyCount; ++p)
                {
                    if (IsAnimatable(static_cast<StyleProperty>(p)))
                    {
                        entry[p] = i;
                    }
                }
                continue;
            }
            for (const StyleProperty property : PropertiesOf(names[i]))
            {
                if (IsAnimatable(property))
                {
                    entry[static_cast<std::size_t>(property)] = i;
                }
            }
        }

        std::vector<RunningTransition>& running = motion.transitions;
        for (std::size_t p = 0; p < StylePropertyCount; ++p)
        {
            const auto property = static_cast<StyleProperty>(p);
            if (!IsAnimatable(property) || SameField(property, before, after))
            {
                continue;
            }
            const auto existing = std::find_if(running.begin(), running.end(),
                                               [&](const RunningTransition& t) { return t.property == property; });
            // The value it shows now: part way through a running transition, or its old cascaded value.
            ResolvedStyle current;
            CopyField(property, before, current);
            if (existing != running.end())
            {
                ApplyTransition(*existing, current);
                running.erase(existing);
            }
            if (entry[p] == none)
            {
                continue;
            }
            RunningTransition transition;
            transition.property = property;
            transition.duration = std::max(At(after.transitionDuration, entry[p], 0.0f), 0.0f);
            transition.delay = At(after.transitionDelay, entry[p], 0.0f);
            transition.timing = At(after.transitionTimingFunction, entry[p], TimingFunction::Ease());
            // As CSS: a transition that would end before it starts does not run, nor one to the value it shows.
            if (transition.duration + transition.delay <= 0.0f || SameField(property, current, after))
            {
                continue;
            }
            CopyField(property, current, transition.from);
            CopyField(property, after, transition.to);
            running.push_back(std::move(transition));
        }
    }

    void ReconcileAnimations(ElementMotion& motion, const ResolvedStyle& style, const KeyframesByName& keyframes)
    {
        std::vector<RunningAnimation> next;
        for (std::size_t i = 0; i < style.animationName.size(); ++i)
        {
            const std::string& name = style.animationName[i];
            const auto found = keyframes.find(name);
            if (name == "none" || found == keyframes.end())
            {
                continue;
            }
            const auto playing = std::find_if(motion.animations.begin(), motion.animations.end(),
                                              [&](const RunningAnimation& a) { return a.name == name; });
            RunningAnimation animation;
            if (playing != motion.animations.end())
            {
                animation = std::move(*playing);
                motion.animations.erase(playing);
            }
            animation.name = name;
            animation.index = i;
            animation.keyframes = found->second;
            animation.finished = HasEnded(ParamsOf(style, i), animation.elapsed);
            next.push_back(std::move(animation));
        }
        motion.animations = std::move(next);
    }

    bool AdvanceMotion(ElementMotion& motion, const ResolvedStyle& style, float seconds)
    {
        bool changed = !motion.transitions.empty();
        for (RunningTransition& transition : motion.transitions)
        {
            transition.elapsed += seconds;
        }
        std::erase_if(motion.transitions, [](const RunningTransition& t) { return t.elapsed >= t.delay + t.duration; });
        for (RunningAnimation& animation : motion.animations)
        {
            if (!animation.finished)
            {
                changed = true;
                animation.elapsed += seconds;
                animation.finished = HasEnded(ParamsOf(style, animation.index), animation.elapsed);
            }
        }
        return changed;
    }

    void ApplyMotion(const ElementMotion& motion, ResolvedStyle& style)
    {
        if (!motion.animations.empty())
        {
            const ResolvedStyle underlying = style;
            for (const RunningAnimation& animation : motion.animations)
            {
                const AnimationParams params = ParamsOf(underlying, animation.index);
                const std::optional<float> progress = KeyframeProgress(params, animation.elapsed);
                if (!progress)
                {
                    continue;
                }
                for (std::size_t p = 0; p < StylePropertyCount; ++p)
                {
                    if (animation.keyframes->properties.test(p))
                    {
                        SampleKeyframes(*animation.keyframes, static_cast<StyleProperty>(p), *progress, params.timing,
                                        underlying, style);
                    }
                }
            }
        }
        // As CSS, a transition shows over an animation of the same property.
        for (const RunningTransition& transition : motion.transitions)
        {
            ApplyTransition(transition, style);
        }
    }
} // namespace Fenrir::Detail
