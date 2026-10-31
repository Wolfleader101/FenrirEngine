#include "Animators.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <tuple>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include "AnimationState.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/BoneMaskData.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        constexpr std::uint64_t MaxCount = std::numeric_limits<std::uint64_t>::max();

        std::uint64_t SaturatingAdd(std::uint64_t a, std::uint64_t b)
        {
            return a > MaxCount - b ? MaxCount : a + b;
        }

        std::string Describe(Entity entity)
        {
            if (entity.IsValid() && entity.HasComponent<Name>() && entity.GetComponent<Name>().Get()[0] != '\0')
            {
                return std::format("'{}'", entity.GetComponent<Name>().Get());
            }
            return std::format("entity {}", entity.GetId());
        }

        void SearchKeys(ClipTrack& track)
        {
            for (KeyCursor& key : track.keys)
            {
                key.search = true;
            }
        }

        // Starts a newly named clip's track (ADR-0034): its cursor at the start, its key cursors fresh.
        void StartTrack(ClipTrack& track, const ClipData& data, std::uint32_t revision, float speed)
        {
            track.cursor.Start(data, revision, speed);
            track.keys.assign(data.channels.size(), KeyCursor{});
        }

        // A changed clip: the cursor is placed into its duration, crossing nothing, and the key cursors start fresh.
        void RefitTrack(ClipTrack& track, const ClipData& data, std::uint32_t revision, ClipLoop loop)
        {
            track.cursor.Refit(data, revision, loop);
            track.keys.assign(data.channels.size(), KeyCursor{});
        }

        // Advances a track's cursor; a wrap or a reversal sends its key cursors searching.
        CursorStep StepTrack(ClipTrack& track, const ClipData& data, ClipLoop loop, bool playing, double delta)
        {
            CursorStep step = track.cursor.Step(data, loop, playing, delta);
            if (step.jumped)
            {
                SearchKeys(track);
            }
            return step;
        }

        float CurveOf(FadeCurve curve, double progress)
        {
            const auto t = static_cast<float>(std::clamp(progress, 0.0, 1.0));
            return curve == FadeCurve::Linear ? t : t * t * (3.0f - 2.0f * t);
        }

        // Sets the channels in `covered` of `to` from `from`.
        void CopyChannels(Transform& to, const Transform& from, std::uint8_t covered)
        {
            if ((covered & CoverTranslation) != 0)
            {
                to.pos = from.pos;
            }
            if ((covered & CoverRotation) != 0)
            {
                to.rot = from.rot;
            }
            if ((covered & CoverScale) != 0)
            {
                to.scale = from.scale;
            }
        }

        // One stream of a skeleton's crossings this frame: a clip of one layer, and how far along its step its
        // crossings may fire.
        struct Stream
        {
            Entity layer;
            std::size_t rank = 0;
            AssetHandle<Clip> clip;
            std::uint32_t revision = 0;
            const ClipData* data = nullptr;
            Crossings crossings;
            double limit = Unlimited;
            std::uint64_t sent = 0;
        };

        // One crossing waiting to be sent, ordered by when in the frame it happened.
        struct Pending
        {
            double fraction = 0.0;
            std::size_t rank = 0;
            std::size_t stream = 0;
            std::size_t sequence = 0;
            std::uint32_t index = 0;
            std::uint64_t loop = 0;
        };

        // The stream's earliest eligible crossings, at most `budget`, in playback order.
        void Earliest(const Stream& stream, std::size_t streamIndex, std::uint64_t budget, std::vector<Pending>& out)
        {
            std::vector<Crossing> crossed;
            stream.crossings.Earliest(stream.data->events, stream.limit, budget, crossed);
            for (std::size_t n = 0; n < crossed.size(); ++n)
            {
                out.push_back({crossed[n].fraction, stream.rank, streamIndex, n, crossed[n].index, crossed[n].loop});
            }
        }
    } // namespace

    /** @brief What one `Animate` pass shares across the skeletons it evaluates. */
    struct Animators::Pass
    {
        AnimationState& state;
        App& app;
        ILogger& logger;
        const AssetStore& store;
        Scene& scene;
        double step;
        // The retained base pose a pass evaluates over: a tick pass's is the simulation pose (ADR-0040).
        PoseKind kind;
        std::vector<Stream> streams;
    };

    namespace
    {
        // A layer's fields as the composition reads them: the base `Animator`'s, or an `AnimationLayer`'s.
        struct LayerSpec
        {
            Entity entity;
            bool base = false;
            AssetHandle<Clip> clip;
            float speed = 1.0f;
            ClipLoop loop = ClipLoop::Loop;
            bool playing = true;
            float fadeSeconds = 0.0f;
            FadeCurve fadeCurve = FadeCurve::Smooth;
            float weight = 1.0f;
            LayerMode mode = LayerMode::Override;
            AssetHandle<BoneMask> mask;
            AdditiveReference reference = AdditiveReference::ClipStart;
        };

        LayerSpec SpecOf(Entity skeleton, const Animator& animator)
        {
            LayerSpec spec;
            spec.entity = skeleton;
            spec.base = true;
            spec.clip = animator.clip;
            spec.speed = animator.speed;
            spec.loop = animator.loop;
            spec.playing = animator.playing;
            spec.fadeSeconds = animator.fadeSeconds;
            spec.fadeCurve = animator.fadeCurve;
            return spec;
        }

        LayerSpec SpecOf(Entity entity, const AnimationLayer& layer)
        {
            LayerSpec spec;
            spec.entity = entity;
            spec.clip = layer.clip;
            spec.speed = layer.speed;
            spec.loop = layer.loop;
            spec.playing = layer.playing;
            spec.fadeSeconds = layer.fadeSeconds;
            spec.fadeCurve = layer.fadeCurve;
            spec.weight = std::isfinite(layer.weight) ? std::clamp(layer.weight, 0.0f, 1.0f) : 0.0f;
            spec.mode = layer.mode;
            spec.mask = layer.mask;
            spec.reference = layer.reference;
            return spec;
        }

        // The skeleton a layer evaluates on: its bones and handles.
        struct SkeletonView
        {
            Entity entity;
            AssetHandle<Skeleton> handle;
            std::uint32_t revision = 0;
            const SkeletonData* data = nullptr;
        };

        // Samples a track's clip into `pose` (ADR-0036): local values for an override layer or the base, deltas from
        // the reference pose for an additive one. Bones at mask weight 0 are skipped, their key cursors told to
        // search, so a later sample never trusts a stale one.
        void SampleTrack(const AssetStore& store, ILogger& logger, Bindings& bindings, ReferencePoses& references,
                         const SkeletonView& skeleton, ClipTrack& track, const LayerSpec& spec,
                         const std::vector<float>* mask, LayerPose& pose)
        {
            pose.Reset(skeleton.data->BoneCount());
            const ClipData* clip = track.cursor.Started() ? store.TryGet(track.clip) : nullptr;
            if (clip == nullptr)
            {
                return;
            }
            const std::uint32_t revision = store.Revision(track.clip);
            if (track.keys.size() != clip->channels.size())
            {
                track.keys.assign(clip->channels.size(), KeyCursor{});
            }
            const BindingKey key{track.clip, revision, skeleton.handle, skeleton.revision};
            const Binding& binding = bindings.Get(store, logger, key, *clip, *skeleton.data);
            ReferencePose* reference = spec.mode == LayerMode::Additive
                                           ? &references.Get(key, spec.reference, *clip, binding, *skeleton.data)
                                           : nullptr;
            const auto time = static_cast<float>(track.cursor.Time());
            for (std::size_t c = 0; c < clip->channels.size(); ++c)
            {
                const std::int32_t bone = binding.bones[c];
                if (bone == NoBone)
                {
                    continue;
                }
                const auto b = static_cast<std::size_t>(bone);
                if (mask != nullptr && !((*mask)[b] > 0.0f))
                {
                    track.keys[c].search = true;
                    continue;
                }
                const ClipChannel& channel = clip->channels[c];
                Transform sample;
                ApplySample(channel.path, SampleChannel(channel, time, track.keys[c]), sample);
                Transform& value = pose.values[b];
                if (reference == nullptr)
                {
                    CopyChannels(value, sample, CoverOf(channel.path));
                }
                else
                {
                    const Transform& from = reference->pose[b];
                    switch (channel.path)
                    {
                    case ClipPath::Translation:
                        value.pos = sample.pos - from.pos;
                        break;
                    case ClipPath::Rotation:
                        value.rot = glm::normalize(sample.rot * glm::inverse(from.rot));
                        break;
                    case ClipPath::Scale: {
                        bool degenerate = false;
                        value.scale = ScaleRatio(sample.scale, from.scale, degenerate);
                        if (degenerate && !reference->reportedScale[b])
                        {
                            reference->reportedScale[b] = true;
                            logger.Warn("{}", std::format("FenrirAnimation: additive clip {}.{} (revision {}) scales "
                                                          "bone '{}' from a reference it cannot divide by (zero, "
                                                          "non-finite, or a ratio that is not positive); that scale "
                                                          "component adds nothing",
                                                          track.clip.index, track.clip.generation, revision,
                                                          skeleton.data->bones[b].sourceName));
                        }
                        break;
                    }
                    }
                }
                pose.covered[b] |= CoverOf(channel.path);
            }
        }

        void SkipSampling(LayerPlayback& playback)
        {
            SearchKeys(playback.current);
            if (playback.outgoing)
            {
                SearchKeys(*playback.outgoing);
            }
        }
    } // namespace

    void Animators::Seek(Entity entity, double seconds)
    {
        m_seeks.insert_or_assign(entity.GetId(), std::pair{entity, seconds});
    }

    const LayerPlayback* Animators::FindBase(Entity entity) const
    {
        const auto found = m_bases.find(entity.GetId());
        return found == m_bases.end() || found->second.entity != entity ? nullptr : &found->second;
    }

    const LayerPlayback* Animators::FindLayer(Entity entity) const
    {
        const auto found = m_layers.find(entity.GetId());
        return found == m_layers.end() || found->second.entity != entity ? nullptr : &found->second;
    }

    void Animators::Run(AnimationState& state, AnimateContext& context)
    {
        const AssetStore* store = state.app->TryGetResource<AssetStore>();
        if (store == nullptr)
        {
            return;
        }
        // Reconcile: state for a component that is gone, or whose entity is gone, is released.
        std::erase_if(m_bases, [](const auto& entry) {
            const Entity& entity = entry.second.entity;
            return !entity.IsValid() || !entity.HasComponent<Animator>();
        });
        std::erase_if(m_layers, [](const auto& entry) {
            const Entity& entity = entry.second.entity;
            return !entity.IsValid() || !entity.HasComponent<AnimationLayer>();
        });
        std::erase_if(m_skeletons, [](const auto& entry) {
            const Entity& entity = entry.second.entity;
            return !entity.IsValid() || !entity.HasComponent<SkeletonPose>();
        });
        std::erase_if(m_seeks, [](const auto& entry) { return !entry.second.first.IsValid(); });

        // Each skeleton's layers, by the skeleton entity they name.
        Scene& scene = context.GetScene();
        EntityList& entities = scene.GetEntityList();
        std::unordered_map<std::uint32_t, std::vector<Entity>> layers;
        const std::array<ComponentType, 1> layerTypes{ComponentType::Native<AnimationLayer>()};
        entities.ForEachWith(layerTypes, [&](std::uint32_t id) {
            const Entity entity = entities.GetEntity(id);
            const Entity skeleton = entity.GetComponent<AnimationLayer>().skeleton;
            if (skeleton.IsValid())
            {
                layers[skeleton.GetId()].push_back(entity);
            }
        });

        const PoseKind kind = context.Timing() == PoseTiming::Tick ? PoseKind::Simulation : PoseKind::Render;
        Pass pass{state, *state.app, *state.app->Logger(), *store, scene, context.Step(), kind, {}};
        const std::vector<Entity> none;
        for (const Entity& entity : context.Instances())
        {
            const auto found = layers.find(entity.GetId());
            std::vector<Entity> mine;
            if (found != layers.end())
            {
                for (const Entity& layer : found->second)
                {
                    if (layer.GetComponent<AnimationLayer>().skeleton == entity)
                    {
                        mine.push_back(layer);
                    }
                }
            }
            if (entity.HasComponent<Animator>() || !mine.empty() || m_skeletons.contains(entity.GetId()))
            {
                std::ranges::sort(mine, [](const Entity& a, const Entity& b) {
                    const int orderA = a.GetComponent<AnimationLayer>().order;
                    const int orderB = b.GetComponent<AnimationLayer>().order;
                    return orderA != orderB ? orderA < orderB : a.GetId() < b.GetId();
                });
                Evaluate(pass, entity, mine);
            }
        }
        m_bindings.SweepUnused();
        m_masks.SweepUnused();
        m_references.SweepUnused();
    }

    void Animators::Evaluate(Pass& pass, Entity skeleton, const std::vector<Entity>& layers)
    {
        const AssetHandle<Skeleton> handle = skeleton.GetComponent<SkeletonPose>().skeleton;
        const SkeletonData* data = pass.store.TryGet(handle);
        const std::span<const Transform> retained = Skeletons::GetBaseLocals(pass.scene, skeleton, pass.kind);
        if (data == nullptr || retained.size() != data->BoneCount())
        {
            return;
        }
        const std::size_t bones = data->BoneCount();
        const SkeletonView view{skeleton, handle, pass.store.Revision(handle), data};
        SkeletonBlend& blend = m_skeletons[skeleton.GetId()];
        if (blend.entity != skeleton)
        {
            blend = SkeletonBlend{};
            blend.entity = skeleton;
        }
        // The foundation is the retained base pose, unless it is what this resource wrote last: then the pose under
        // the layers is the one kept, so layers never compound over their own output (ADR-0036).
        const bool ours = blend.written.size() == bones && blend.foundation.size() == bones &&
                          std::equal(retained.begin(), retained.end(), blend.written.begin(),
                                     [](const Transform& a, const Transform& b) {
                                         return a.pos == b.pos && a.rot == b.rot && a.scale == b.scale;
                                     });
        if (!ours)
        {
            blend.foundation.assign(retained.begin(), retained.end());
        }
        // What this resource wrote last is still the base pose, so contributions that stopped must be taken back.
        const bool restore = ours && !blend.written.empty();
        std::vector<Transform> pose = blend.foundation;
        std::vector<Transform> rest(bones);
        for (std::size_t i = 0; i < bones; ++i)
        {
            rest[i] = data->bones[i].rest;
        }

        pass.streams.clear();
        bool baseActive = false;
        bool layered = false;
        // Evaluates one layer: its clip change, clock, fade and events, then its contribution to `pose`.
        const auto evaluate = [&](const LayerSpec& spec, LayerPlayback& playback, std::size_t rank) -> bool {
            ILogger& logger = pass.logger;
            const AssetStore& store = pass.store;
            const std::vector<Transform>& underlying = spec.base ? rest : pose;

            // The mask's weights for this skeleton, or none for a layer reaching every bone.
            const std::vector<float>* mask = nullptr;
            std::vector<float> nothing;
            if (!spec.mask.IsEmpty())
            {
                if (const BoneMaskData* maskData = store.TryGet(spec.mask))
                {
                    playback.reportedMask = false;
                    mask =
                        &m_masks
                             .Get(logger, spec.mask, store.Revision(spec.mask), *maskData, handle, view.revision, *data)
                             .weights;
                }
                else
                {
                    if (!playback.reportedMask)
                    {
                        playback.reportedMask = true;
                        logger.Warn("{}", std::format("FenrirAnimation: the bone mask {}.{} that {}'s AnimationLayer "
                                                      "names does not resolve, so the layer reaches no bone",
                                                      spec.mask.index, spec.mask.generation, Describe(spec.entity)));
                    }
                    nothing.assign(bones, 0.0f);
                    mask = &nothing;
                }
            }

            // The layer's value per bone, blending a fade's outgoing source into the current clip, coverage-aware:
            // a side that does not cover a channel gives the underlying value, or the identity delta (ADR-0036).
            const auto value = [&](const std::vector<float>* reach, LayerPose& result) {
                LayerPose in;
                SampleTrack(store, logger, m_bindings, m_references, view, playback.current, spec, reach, in);
                LayerPose sampledOut;
                const LayerPose* out = nullptr;
                if (playback.fading && playback.snapshot)
                {
                    out = &*playback.snapshot;
                }
                else if (playback.fading && playback.outgoing)
                {
                    SampleTrack(store, logger, m_bindings, m_references, view, *playback.outgoing, spec, reach,
                                sampledOut);
                    out = &sampledOut;
                }
                const float c = playback.fading ? CurveOf(playback.fadeCurve, playback.fadeProgress) : 1.0f;
                result.Reset(bones);
                for (std::size_t b = 0; b < bones; ++b)
                {
                    const std::uint8_t outCovers = out != nullptr && b < out->covered.size() ? out->covered[b] : 0;
                    const std::uint8_t covered = in.covered[b] | outCovers;
                    if (covered == 0)
                    {
                        continue;
                    }
                    const Transform fallback = spec.mode == LayerMode::Additive ? Transform{} : underlying[b];
                    Transform from = fallback;
                    Transform to = fallback;
                    if (out != nullptr)
                    {
                        CopyChannels(from, out->values[b], outCovers);
                    }
                    CopyChannels(to, in.values[b], in.covered[b]);
                    if (playback.fading)
                    {
                        BlendChannels(from, to, covered, c);
                        result.values[b] = from;
                    }
                    else
                    {
                        result.values[b] = to;
                    }
                    result.covered[b] = covered;
                }
            };

            // A clip change: a fade from the outgoing clip, from a snapshot of an interrupted fade, from the retained
            // pose (the base from no clip) or from the underlying pose (a layer from no clip); or a cut.
            if (spec.clip != playback.current.clip)
            {
                const double seconds =
                    std::isfinite(spec.fadeSeconds) && spec.fadeSeconds > 0.0f ? spec.fadeSeconds : 0.0;
                if (seconds > 0.0 && !(spec.base && spec.clip.IsEmpty()))
                {
                    if (playback.fading)
                    {
                        LayerPose snapshot;
                        value(nullptr, snapshot);
                        playback.snapshot = std::move(snapshot);
                        playback.outgoing.reset();
                    }
                    else if (!playback.current.clip.IsEmpty() && playback.current.cursor.Started())
                    {
                        playback.outgoing = playback.current;
                        playback.snapshot.reset();
                    }
                    else if (spec.base)
                    {
                        LayerPose snapshot;
                        snapshot.values = pose;
                        snapshot.covered.assign(bones, CoverAll);
                        playback.snapshot = std::move(snapshot);
                        playback.outgoing.reset();
                    }
                    else
                    {
                        playback.outgoing.reset();
                        playback.snapshot.reset();
                    }
                    playback.fading = true;
                    playback.fadeProgress = 0.0;
                    playback.fadeSeconds = seconds;
                    playback.fadeCurve = spec.fadeCurve;
                }
                else
                {
                    playback.outgoing.reset();
                    playback.snapshot.reset();
                    playback.fading = false;
                    playback.fadeProgress = 0.0;
                }
                playback.current = ClipTrack{};
                playback.current.clip = spec.clip;
            }

            // The current clip: a stale handle freezes the layer, contributing nothing and not advancing.
            const ClipData* clip = spec.clip.IsEmpty() ? nullptr : store.TryGet(spec.clip);
            if (!spec.clip.IsEmpty() && clip == nullptr)
            {
                playback.unavailable = true;
                if (!playback.reportedUnavailable)
                {
                    playback.reportedUnavailable = true;
                    logger.Warn("{}", std::format("FenrirAnimation: the clip {}.{} that {}'s {} names does not "
                                                  "resolve; its playback is frozen until it does",
                                                  spec.clip.index, spec.clip.generation, Describe(spec.entity),
                                                  spec.base ? "Animator" : "AnimationLayer"));
                }
                return false;
            }
            playback.unavailable = false;
            playback.reportedUnavailable = false;
            if (clip == nullptr && !playback.fading)
            {
                // No clip and no fade: nothing to contribute, and any pending seek waits for a clip.
                return false;
            }

            std::uint32_t revision = 0;
            if (clip != nullptr)
            {
                revision = store.Revision(spec.clip);
                if (!playback.current.cursor.Started())
                {
                    StartTrack(playback.current, *clip, revision, spec.speed);
                }
                else if (revision != playback.current.cursor.Revision())
                {
                    RefitTrack(playback.current, *clip, revision, spec.loop);
                }
                // A seek moves only the current clip's cursor; an Animator's wins on an entity with both.
                const bool seeks = spec.base || !spec.entity.HasComponent<Animator>();
                if (const auto seek = m_seeks.find(spec.entity.GetId()); seeks && seek != m_seeks.end())
                {
                    if (std::isfinite(seek->second.second))
                    {
                        playback.current.cursor.Seek(seek->second.second, spec.loop);
                        SearchKeys(playback.current);
                    }
                    else
                    {
                        logger.Warn("{}", std::format("FenrirAnimation: a seek of {} to a non-finite time was ignored",
                                                      Describe(spec.entity)));
                    }
                    m_seeks.erase(seek);
                }
            }

            // The clocks: the current clip, the outgoing one on its own cursor, and the fade by unscaled time.
            const double delta = spec.playing ? pass.step * static_cast<double>(spec.speed) : 0.0;
            CursorStep current;
            if (clip != nullptr)
            {
                current = StepTrack(playback.current, *clip, spec.loop, spec.playing, delta);
                if (current.finishedNow)
                {
                    pass.app.SendEvent(ClipFinished{skeleton, spec.entity, spec.clip});
                }
            }
            CursorStep outgoing;
            const ClipData* outgoingClip = nullptr;
            if (playback.outgoing)
            {
                outgoingClip = store.TryGet(playback.outgoing->clip);
                if (outgoingClip == nullptr)
                {
                    playback.outgoing.reset();
                }
                else
                {
                    const std::uint32_t outRevision = store.Revision(playback.outgoing->clip);
                    if (outRevision != playback.outgoing->cursor.Revision())
                    {
                        RefitTrack(*playback.outgoing, *outgoingClip, outRevision, spec.loop);
                    }
                    // An outgoing Once clip that ends holds there and sends nothing.
                    outgoing = StepTrack(*playback.outgoing, *outgoingClip, spec.loop, spec.playing, delta);
                }
            }
            double limit = Unlimited;
            if (playback.fading && spec.playing)
            {
                const double from = playback.fadeProgress;
                const double rate = pass.step / playback.fadeSeconds;
                const double ends = rate > 0.0 ? (1.0 - from) / rate : Unlimited;
                if (ends < 1.0 && outgoing.crossings)
                {
                    // The outgoing clip may fire until the fade completes, at this fraction of the frame.
                    limit = ends * outgoing.crossings->Length();
                }
                playback.fadeProgress = std::min(1.0, from + rate);
            }

            // Events from contributing clips only: the layer above weight 0, the incoming clip from its fade's start,
            // the outgoing one until its end, a snapshot never (ADR-0036).
            if (spec.weight > 0.0f)
            {
                if (current.crossings)
                {
                    pass.streams.push_back({spec.entity, rank, spec.clip, revision, clip, *current.crossings});
                }
                if (outgoing.crossings && playback.outgoing)
                {
                    pass.streams.push_back({spec.entity, rank, playback.outgoing->clip,
                                            playback.outgoing->cursor.Revision(), outgoingClip, *outgoing.crossings,
                                            limit});
                }
            }
            if (playback.fading && playback.fadeProgress >= 1.0)
            {
                playback.fading = false;
                playback.fadeProgress = 0.0;
                playback.outgoing.reset();
                playback.snapshot.reset();
            }
            if (clip == nullptr && !playback.fading)
            {
                // A fade to no clip has ended: the layer's contribution is cleared, its entity kept.
                return false;
            }

            // The contribution, unless the layer is at weight 0: then nothing is sampled, and its key cursors search
            // when it returns.
            if (!(spec.weight > 0.0f))
            {
                SkipSampling(playback);
                return false;
            }
            LayerPose result;
            value(mask, result);
            if (spec.base)
            {
                pose = rest;
            }
            for (std::size_t b = 0; b < bones; ++b)
            {
                if (result.covered[b] == 0)
                {
                    continue;
                }
                const float w = std::clamp(spec.weight * (mask != nullptr ? (*mask)[b] : 1.0f), 0.0f, 1.0f);
                if (!(w > 0.0f))
                {
                    continue;
                }
                if (spec.mode == LayerMode::Additive && !spec.base)
                {
                    ApplyAdditive(pose[b], result.values[b], result.covered[b], w);
                }
                else
                {
                    ApplyOverride(pose[b], result.values[b], result.covered[b], w);
                }
            }
            return true;
        };

        std::size_t rank = 0;
        if (skeleton.HasComponent<Animator>())
        {
            LayerPlayback& playback = m_bases[skeleton.GetId()];
            if (playback.entity != skeleton)
            {
                playback = LayerPlayback{};
                playback.entity = skeleton;
                playback.skeleton = skeleton;
            }
            baseActive = evaluate(SpecOf(skeleton, skeleton.GetComponent<Animator>()), playback, rank);
            if (baseActive)
            {
                blend.foundation = pose;
            }
        }
        for (const Entity& entity : layers)
        {
            ++rank;
            LayerPlayback& playback = m_layers[entity.GetId()];
            if (playback.entity != entity || playback.skeleton != skeleton)
            {
                // A new layer, or one retargeted to another skeleton: it restarts, with no fade.
                playback = LayerPlayback{};
                playback.entity = entity;
                playback.skeleton = skeleton;
            }
            evaluate(SpecOf(entity, entity.GetComponent<AnimationLayer>()), playback, rank);
            layered = true;
        }

        // The base pose is written when the base layer resolved it or any layer exists, and once more when a layer
        // that contributed is gone, to take its contribution back; otherwise it is left alone.
        if (baseActive || layered || restore)
        {
            std::span<Transform> base = Skeletons::EditBasePose(pass.scene, skeleton);
            if (base.size() == bones)
            {
                std::ranges::copy(pose, base.begin());
                blend.written = pose;
            }
            if (!baseActive && !layered)
            {
                blend.written.clear();
            }
        }
        else
        {
            m_skeletons.erase(skeleton.GetId());
            return;
        }

        // One chronological stream per skeleton: the earliest crossings of every layer, by frame fraction, then layer
        // order, sent up to the shared budget; the rest counted per layer (ADR-0036).
        const std::uint64_t budget = pass.state.config.maxEventsPerInstancePerFrame;
        std::vector<Pending> pending;
        for (std::size_t s = 0; s < pass.streams.size(); ++s)
        {
            Earliest(pass.streams[s], s, budget, pending);
        }
        std::ranges::sort(pending, [](const Pending& a, const Pending& b) {
            return std::tie(a.fraction, a.rank, a.stream, a.sequence) <
                   std::tie(b.fraction, b.rank, b.stream, b.sequence);
        });
        if (pending.size() > budget)
        {
            pending.resize(static_cast<std::size_t>(budget));
        }
        for (const Pending& crossing : pending)
        {
            Stream& stream = pass.streams[crossing.stream];
            const ClipEvent& event = stream.data->events[crossing.index];
            pass.app.SendEvent(AnimationEvent{skeleton, stream.layer, stream.clip, stream.revision, crossing.index,
                                              ClipEventId::Of(event.name), event.time, crossing.loop});
            ++stream.sent;
        }
        std::uint64_t droppedTotal = 0;
        for (const Stream& stream : pass.streams)
        {
            const std::uint64_t eligible = stream.crossings.CountBefore(stream.data->events, stream.limit);
            const std::uint64_t dropped = eligible > stream.sent ? eligible - stream.sent : 0;
            if (dropped != 0)
            {
                pass.app.SendEvent(AnimationEventsDropped{skeleton, stream.layer, stream.clip, dropped});
                droppedTotal = SaturatingAdd(droppedTotal, dropped);
            }
        }
        if (droppedTotal != 0 && !blend.reportedDrop)
        {
            blend.reportedDrop = true;
            pass.logger.Warn("{}", std::format("FenrirAnimation: {} crossed more clip events in one frame than its "
                                               "budget of {}; {} were dropped (logged once per skeleton)",
                                               Describe(skeleton), budget, droppedTotal));
        }
    }
} // namespace Fenrir::Detail
