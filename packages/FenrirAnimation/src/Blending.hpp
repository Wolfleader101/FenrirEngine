#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include "Bindings.hpp"
#include "FenrirAnimation/AnimationLayer.hpp"
#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class AssetStore;
    class ILogger;
    struct BoneMaskData;
    struct SkeletonData;

    namespace Detail
    {
        /** @brief Which of a bone's channels a layer value covers: one bit per path. */
        enum Cover : std::uint8_t
        {
            CoverTranslation = 1,
            CoverRotation = 2,
            CoverScale = 4,
            CoverAll = 7,
        };

        [[nodiscard]] std::uint8_t CoverOf(ClipPath path) noexcept;

        /**
         * @brief One layer's value for each bone, with the channels it covers (ADR-0036): local values for an override
         * layer or the base, deltas for an additive layer. What a crossfade's outgoing snapshot holds.
         */
        struct LayerPose
        {
            std::vector<Transform> values;
            std::vector<std::uint8_t> covered;

            void Reset(std::size_t bones)
            {
                values.assign(bones, Transform{});
                covered.assign(bones, 0);
            }
        };

        /** @brief `a` to `b` by `t` along the shorter arc, normalised. */
        [[nodiscard]] Math::Quat Nlerp(const Math::Quat& a, const Math::Quat& b, float t) noexcept;

        /** @brief The channels in `covered` of `a` blended toward `b` by `t`: lerp, and nlerp for rotation. */
        void BlendChannels(Transform& a, const Transform& b, std::uint8_t covered, float t) noexcept;

        /** @brief An override layer's `value` over `current` at weight `w`, on the channels in `covered`. */
        void ApplyOverride(Transform& current, const Transform& value, std::uint8_t covered, float w) noexcept;

        /**
         * @brief An additive layer's `delta` onto `current` at weight `w`, on the channels in `covered`: translation
         * adds `delta × w`, rotation is `slerp(identity, delta, w) × current`, scale multiplies by `lerp(1, delta, w)`.
         */
        void ApplyAdditive(Transform& current, const Transform& delta, std::uint8_t covered, float w) noexcept;

        /**
         * @brief The additive scale ratio `sample / reference` per component; a component whose reference or sample is
         * non-finite, whose `|reference| < 1e-6` or whose ratio is not positive gives 1, and sets `degenerate`.
         */
        [[nodiscard]] Math::Vec3 ScaleRatio(const Math::Vec3& sample, const Math::Vec3& reference,
                                            bool& degenerate) noexcept;

        /** @brief A mask resolved for one skeleton: each bone's weight, or no weights for a mask reaching every bone.
         */
        struct MaskBinding
        {
            std::vector<float> weights;
            bool used = false;
        };

        /** @brief What a mask binding or a reference pose is for: an asset and a skeleton, each at one revision. */
        struct PairKey
        {
            std::uint32_t asset = 0;
            std::uint32_t assetGeneration = 0;
            std::uint32_t assetRevision = 0;
            std::uint32_t skeleton = 0;
            std::uint32_t skeletonGeneration = 0;
            std::uint32_t skeletonRevision = 0;
            std::uint8_t mode = 0;

            friend auto operator<=>(const PairKey&, const PairKey&) = default;
        };

        /** @brief The masks in use, resolved per (mask, skeleton) revisions and dropped when no layer uses them. */
        class MaskBindings
        {
          public:
            /** @brief The mask's weights for the skeleton, built (and unmatched names logged) the first time. */
            const MaskBinding& Get(ILogger& logger, AssetHandle<BoneMask> mask, std::uint32_t maskRevision,
                                   const BoneMaskData& data, AssetHandle<Skeleton> skeleton,
                                   std::uint32_t skeletonRevision, const SkeletonData& bones);

            void SweepUnused();

          private:
            std::map<PairKey, MaskBinding> m_masks;
        };

        /** @brief A complete local reference pose for an additive clip, and which bones' scale was diagnosed. */
        struct ReferencePose
        {
            std::vector<Transform> pose;
            std::vector<bool> reportedScale;
            bool used = false;
        };

        /** @brief The reference poses in use, per (clip, skeleton, mode) revisions, dropped when no layer uses them. */
        class ReferencePoses
        {
          public:
            ReferencePose& Get(const BindingKey& key, AdditiveReference mode, const ClipData& clip,
                               const Binding& binding, const SkeletonData& skeleton);

            void SweepUnused();

          private:
            std::map<PairKey, ReferencePose> m_poses;
        };
    } // namespace Detail
} // namespace Fenrir
