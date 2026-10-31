#include "FenrirSkeleton/Skeletons.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScheduler/Scheduler.hpp"
#include "SkeletonPoses.hpp"
#include "SkeletonsState.hpp"

namespace Fenrir
{
    namespace
    {
        using Detail::PipelineStep;
        using Detail::SkeletonInstance;
        using Detail::SkeletonPoses;

        SkeletonPoses& PosesOf(Scene& scene, ILogger& logger)
        {
            if (SkeletonPoses* poses = scene.GetResource<SkeletonPoses>())
            {
                return *poses;
            }
            return scene.AddResource<SkeletonPoses>(logger);
        }

        // The entity's instance when it is alive and Ready in this scene; null otherwise.
        SkeletonInstance* ReadyIn(Scene& scene, Entity skeleton)
        {
            SkeletonPoses* poses = scene.GetResource<SkeletonPoses>();
            return poses == nullptr || !skeleton.IsValid() ? nullptr : poses->FindReady(skeleton);
        }

        bool InRange(const SkeletonInstance& instance, std::int32_t bone)
        {
            return bone >= 0 && static_cast<std::size_t>(bone) < instance.base.size();
        }

        // Whether a read of `kind` gives the instance's simulation pose: the last tick's, on a tick-posed instance.
        // On a frame-posed one both kinds read the render pose (ADR-0040).
        bool ReadsTick(const SkeletonInstance& instance, PoseKind kind)
        {
            return kind == PoseKind::Simulation && instance.timing == PoseTiming::Tick;
        }

        // Whether the active scene has a tick-posed skeleton, so a tick pass has anything to do.
        bool HasTickPosed(Scene& scene)
        {
            EntityList& entities = scene.GetEntityList();
            bool found = false;
            const std::array<ComponentType, 1> poses{ComponentType::Native<SkeletonPose>()};
            entities.ForEachWith(poses, [&](std::uint32_t id) {
                found = found || entities.GetEntity(id).GetComponent<SkeletonPose>().timing == PoseTiming::Tick;
            });
            return found;
        }

        std::string_view StepName(PipelineStep step)
        {
            switch (step)
            {
            case PipelineStep::Prepare:
                return "prepare";
            case PipelineStep::Modify:
                return "Modify";
            case PipelineStep::Finalise:
                return "finalise";
            case PipelineStep::Attach:
                return "attach";
            case PipelineStep::Late:
                return "Late";
            default:
                return "the pose pipeline";
            }
        }

        std::string_view TimingName(PoseTiming timing)
        {
            return timing == PoseTiming::Tick ? "tick" : "frame";
        }

        // Ends the pipeline's run however the stage work leaves it, so a throw cannot leave the scene mid-step.
        class PipelineRun
        {
          public:
            PipelineRun(Detail::SkeletonsState& state, SkeletonPoses& poses) noexcept : m_state(state), m_poses(poses)
            {
                m_state.running = true;
            }
            PipelineRun(const PipelineRun&) = delete;
            PipelineRun& operator=(const PipelineRun&) = delete;
            ~PipelineRun()
            {
                m_poses.step = PipelineStep::None;
                m_state.running = false;
            }

          private:
            Detail::SkeletonsState& m_state;
            SkeletonPoses& m_poses;
        };

        // A stage's work as it stands when the stage starts, so work added meanwhile waits for the next frame.
        template <typename Context>
        void RunStage(const Detail::Stage<Context>& stage, Context& context)
        {
            const std::vector<Detail::StageWork<Context>> snapshot = stage.work;
            for (const Detail::StageWork<Context>& entry : snapshot)
            {
                (*entry.work)(context);
            }
        }
    } // namespace

    // The pose pipeline's passes on the active scene; a friend of the stage contexts, which only it makes.
    struct Detail::PosePipeline
    {
        // A tick pass (ADR-0040), in PreTick: tick-posed instances evaluate their simulation pose, the base pose at
        // tick time with no modifiers, keeping the last tick's beside it.
        static void RunTick(Detail::SkeletonsState& state, App& app)
        {
            AssetStore* store = app.TryGetResource<AssetStore>();
            Scene& scene = app.GetActiveScene();
            if (store == nullptr || !HasTickPosed(scene))
            {
                return;
            }
            SkeletonPoses& poses = PosesOf(scene, *app.Logger());
            const PipelineRun run(state, poses);

            poses.step = PipelineStep::Reconcile;
            poses.ReconcileAll(*store, scene);
            const std::vector<Entity> instances = poses.ReadyEntities(PoseTiming::Tick);
            // The last tick's pose becomes the previous one; Animate writes this tick's over a copy of it, so a
            // tick-posed skeleton nothing animates keeps its pose.
            poses.ForEach([](SkeletonInstance& instance) {
                if (instance.ready && instance.timing == PoseTiming::Tick)
                {
                    instance.previousTickBase = instance.tickBase;
                }
            });

            poses.step = PipelineStep::Animate;
            poses.passTiming = PoseTiming::Tick;
            AnimateContext animate(scene, PoseTiming::Tick, app.GetTime().tickRate, instances);
            RunStage(state.animate, animate);

            // The base finalise: the simulation pose's model matrices, parents first.
            poses.step = PipelineStep::Finalise;
            poses.ForEach([](SkeletonInstance& instance) {
                if (instance.ready && instance.timing == PoseTiming::Tick)
                {
                    SkeletonPoses::AllTickModels(instance);
                }
            });
        }

        static void Run(Detail::SkeletonsState& state, App& app)
        {
            AssetStore* store = app.TryGetResource<AssetStore>();
            if (store == nullptr)
            {
                return;
            }
            Scene& scene = app.GetActiveScene();
            SkeletonPoses& poses = PosesOf(scene, *app.Logger());
            poses.frame = app.GetTime().frameCount;
            const PipelineRun run(state, poses);

            poses.step = PipelineStep::Reconcile;
            poses.ReconcileAll(*store, scene);

            // A tick-posed instance skips Animate: its base pose for the frame is its last two ticks' blended at the
            // fraction physics interpolates bodies by.
            const Time& time = app.GetTime();
            const auto fraction =
                static_cast<float>(time.tickRate > 0.0 ? std::clamp(time.accumulator / time.tickRate, 0.0, 1.0) : 1.0);
            poses.ForEach([fraction](SkeletonInstance& instance) {
                if (instance.ready && instance.timing == PoseTiming::Tick)
                {
                    SkeletonPoses::BlendTicks(instance, fraction);
                }
            });

            poses.step = PipelineStep::Animate;
            poses.passTiming = PoseTiming::Frame;
            const std::vector<Entity> instances = poses.ReadyEntities(PoseTiming::Frame);
            AnimateContext animate(scene, PoseTiming::Frame, app.GetTime().deltaTime, instances);
            RunStage(state.animate, animate);

            poses.step = PipelineStep::Prepare;
            poses.ForEach([](SkeletonInstance& instance) {
                // A clean instance that no modifier edited last frame already has final = base and valid matrices.
                if (instance.ready && (instance.poseDirty || instance.modifiedLastFrame))
                {
                    SkeletonPoses::Prepare(instance);
                }
            });

            poses.step = PipelineStep::Modify;
            for (const Detail::StageWork<ModifierContext>& entry : std::vector(state.modify.work))
            {
                ModifierContext modify(scene, poses, ++poses.modifierCall);
                (*entry.work)(modify);
            }
            // An edit kept past its modifier call must not match the next frame's first call.
            ++poses.modifierCall;

            poses.step = PipelineStep::Finalise;
            poses.ForEach([&poses](SkeletonInstance& instance) {
                if (instance.ready && instance.prepared)
                {
                    poses.Finalise(instance);
                }
                else
                {
                    instance.modifiedLastFrame = false;
                }
            });

            poses.step = PipelineStep::Attach;
            poses.attachments.Place(scene, poses);

            poses.step = PipelineStep::Late;
            LateContext late(scene);
            RunStage(state.late, late);
        }
    };

    namespace
    {
        void SpawnSkeletonsFor(Detail::SkeletonsState& state, Scene& scene, AssetHandle<Model> model,
                               const SpawnedNodes& spawned)
        {
            ILogger& logger = *state.app->Logger();
            AssetStore* store = state.app->TryGetResource<AssetStore>();
            const Model* data = store == nullptr ? nullptr : store->TryGet(model);
            if (data == nullptr)
            {
                logger.Error("{}", "Skeletons::SpawnSkeletons: the model handle does not resolve; no skeleton spawned");
                return;
            }
            SkeletonPoses& poses = PosesOf(scene, logger);
            for (std::size_t k = 0; k < data->skeletons.size(); ++k)
            {
                const AssetHandle<Skeleton> skeleton = data->skeletons[k];
                if (!store->IsValid(skeleton))
                {
                    // The skeleton failed to load, or was freed: its owner stays an ordinary entity.
                    continue;
                }
                const std::int32_t node = k < data->skeletonOwners.size() ? data->skeletonOwners[k] : -1;
                const Entity owner = node >= 0 && static_cast<std::size_t>(node) < spawned.nodes.size()
                                         ? spawned.nodes[static_cast<std::size_t>(node)]
                                         : Entity();
                if (!owner.IsValid())
                {
                    logger.Warn("{}", std::format("Skeletons::SpawnSkeletons: skeleton {} of the model has no spawned "
                                                  "owner (node {}), so it gets no SkeletonPose",
                                                  k, node));
                    continue;
                }
                if (owner.HasComponent<SkeletonPose>())
                {
                    logger.Warn("{}",
                                std::format("Skeletons::SpawnSkeletons: the owner of skeleton {}, {}, already has "
                                            "a SkeletonPose, which it keeps",
                                            k, Detail::Describe(owner)));
                    continue;
                }
                Entity(owner).AddComponent<SkeletonPose>(SkeletonPose{skeleton});
                poses.Reconcile(*store, owner);
            }

            // Every ordinary node hanging from a bone follows it: attached to that bone with its rest place relative to
            // it, which SpawnNodes composed into its Transform through the bones above.
            for (std::size_t i = 0; i < data->nodes.size() && i < spawned.nodes.size(); ++i)
            {
                const ModelNode& node = data->nodes[i];
                const Entity& entity = spawned.nodes[i];
                if (node.kind == ModelNodeKind::Bone || node.parent < 0 || !entity.IsValid())
                {
                    continue;
                }
                const ModelNode& above = data->nodes[static_cast<std::size_t>(node.parent)];
                if (above.kind != ModelNodeKind::Bone || above.skeleton < 0 ||
                    static_cast<std::size_t>(above.skeleton) >= data->skeletonOwners.size())
                {
                    continue;
                }
                const std::int32_t ownerNode = data->skeletonOwners[static_cast<std::size_t>(above.skeleton)];
                const Entity owner = ownerNode >= 0 && static_cast<std::size_t>(ownerNode) < spawned.nodes.size()
                                         ? spawned.nodes[static_cast<std::size_t>(ownerNode)]
                                         : Entity();
                const SkeletonData* bones = store->TryGet(data->skeletons[static_cast<std::size_t>(above.skeleton)]);
                if (!owner.IsValid() || !owner.HasComponent<SkeletonPose>() || bones == nullptr)
                {
                    // Its skeleton did not load: the node stays where SpawnNodes put it.
                    continue;
                }
                BoneAttachment attachment;
                attachment.skeleton = owner;
                attachment.offset = node.rest;
                if (attachment.bone.Set(bones->EngineName(above.bone), logger))
                {
                    Entity(entity).AddComponent<BoneAttachment>(attachment);
                }
            }
        }

        // FenrirSkeleton's skinning source (ADR-0041): what the renderer reads, answered from the static reads.
        class SkeletonsSkinningSource final : public SkinningSource
        {
          public:
            explicit SkeletonsSkinningSource(Detail::SkeletonsState& state) noexcept : m_state(&state)
            {
            }

            PoseState GetPoseState(Scene& scene, Entity skeleton) override
            {
                return Skeletons::GetPoseState(scene, skeleton);
            }

            AssetHandle<Skeleton> GetSkeleton(Scene& scene, Entity skeleton) override
            {
                SkeletonPoses* poses = scene.GetResource<SkeletonPoses>();
                const SkeletonInstance* instance =
                    poses == nullptr || !skeleton.IsValid() ? nullptr : poses->Find(skeleton);
                return instance == nullptr ? AssetHandle<Skeleton>{} : instance->skeleton;
            }

            std::uint64_t GetPoseGeneration(Scene& scene, Entity skeleton) override
            {
                return Skeletons::GetPoseGeneration(scene, skeleton);
            }

            std::span<const Math::Mat4> GetPalette(Scene& scene, Entity skeleton) override
            {
                return Skeletons::GetPalette(scene, skeleton);
            }

            std::span<const Math::Mat4> GetModelMatrices(Scene& scene, Entity skeleton) override
            {
                return Skeletons::GetModelMatrices(scene, skeleton);
            }

            void SpawnSkeletons(Scene& scene, AssetHandle<Model> model, const SpawnedNodes& spawned) override
            {
                SpawnSkeletonsFor(*m_state, scene, model, spawned);
            }

          private:
            Detail::SkeletonsState* m_state;
        };

        // Writes every instance's override entries, parents first, at their effective weights.
        void RunPoseOverride(ModifierContext& context)
        {
            SkeletonPoses* poses = context.GetScene().GetResource<SkeletonPoses>();
            if (poses == nullptr)
            {
                return;
            }
            std::vector<Entity> overridden;
            poses->ForEach([&](SkeletonInstance& instance) {
                if (instance.ready && instance.overrideCount != 0)
                {
                    overridden.push_back(instance.entity);
                }
            });
            for (const Entity& skeleton : overridden)
            {
                PoseEdit edit = context.Edit(skeleton);
                const SkeletonInstance* instance = poses->FindReady(skeleton);
                if (edit.IsEmpty() || instance == nullptr)
                {
                    continue;
                }
                for (std::size_t bone = 0; bone < instance->overrides.size(); ++bone)
                {
                    const std::optional<Target>& entry = instance->overrides[bone];
                    if (!entry)
                    {
                        continue;
                    }
                    const float weight = std::clamp(entry->weight * instance->overrideWeights[bone], 0.0f, 1.0f);
                    if (!(weight > 0.0f))
                    {
                        continue;
                    }
                    Target target = *entry;
                    target.weight = weight;
                    edit.Write(static_cast<std::int32_t>(bone), target);
                }
            }
        }

        // The bones a weight assignment reaches: the bone, and with BoneSubtree everything below it.
        std::vector<std::int32_t> Reach(const SkeletonInstance& instance, std::int32_t bone, OverrideScope scope)
        {
            if (scope == OverrideScope::Bone)
            {
                return {bone};
            }
            return SubtreeOf(instance.parents.size(), bone, [&instance](std::size_t i) { return instance.parents[i]; });
        }
    } // namespace

    namespace
    {
        // The hosted pass-end work, as it stands when the pass returns, given the pass's timing.
        void RunPassEnds(const Detail::SkeletonsState& state, PoseTiming timing)
        {
            const auto snapshot = state.passEnds;
            for (const auto& entry : snapshot)
            {
                (*entry.second)(timing);
            }
        }
    } // namespace

    Skeletons Skeletons::Install(App& app)
    {
        if (app.TryGetResource<AssetStore>() == nullptr)
        {
            app.Logger()->Error("{}", "Skeletons::Install: FenrirAssets is not installed, so FenrirSkeleton is off: "
                                      "call Assets::Install first");
            return Skeletons(nullptr);
        }
        if (app.TryGetResource<SkinningSourceRegistration>() != nullptr)
        {
            app.Logger()->Error("{}", "Skeletons::Install: a skinning source is already registered; this second "
                                      "install is ignored and adds nothing");
            return Skeletons(nullptr);
        }
        auto state = std::make_shared<Detail::SkeletonsState>(app);
        state->source = std::make_unique<SkeletonsSkinningSource>(*state);
        app.AddResource<SkinningSourceRegistration>(SkinningSourceRegistration{state->source.get()});
        app.AddResource<Detail::SkeletonsHost>(Detail::SkeletonsHost{state});
        // PoseOverride, the first modifier (ADR-0033, ADR-0038): each instance's entries in bone order, parents first.
        state->modify.Add({state->nextId++, "PoseOverride", ModifierOrder::Override,
                           std::make_shared<std::function<void(ModifierContext&)>>(RunPoseOverride)});
        app.AddSequentialSystem(SchedulePriority::PreTick,
                                [installed = std::weak_ptr<Detail::SkeletonsState>(state)](App& running) {
                                    if (const std::shared_ptr<Detail::SkeletonsState> current = installed.lock())
                                    {
                                        Detail::PosePipeline::RunTick(*current, running);
                                        RunPassEnds(*current, PoseTiming::Tick);
                                    }
                                });
        app.AddSequentialSystem(SchedulePriority::PostUpdate,
                                [installed = std::weak_ptr<Detail::SkeletonsState>(state)](App& running) {
                                    if (const std::shared_ptr<Detail::SkeletonsState> current = installed.lock())
                                    {
                                        Detail::PosePipeline::Run(*current, running);
                                        RunPassEnds(*current, PoseTiming::Frame);
                                    }
                                });
        return Skeletons(std::move(state));
    }

    Skeletons::Skeletons(std::shared_ptr<Detail::SkeletonsState> state) noexcept : m_state(std::move(state))
    {
    }

    Skeletons::Skeletons(Skeletons&& other) noexcept = default;

    Skeletons& Skeletons::operator=(Skeletons&& other) noexcept
    {
        if (this != &other)
        {
            Shutdown();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    Skeletons::~Skeletons()
    {
        Shutdown();
    }

    bool Skeletons::IsValid() const noexcept
    {
        return m_state != nullptr;
    }

    void Skeletons::Shutdown() noexcept
    {
        if (!m_state)
        {
            return;
        }
        App& app = *m_state->app;
        const SkinningSourceRegistration* registration = app.TryGetResource<SkinningSourceRegistration>();
        if (registration != nullptr && registration->source == m_state->source.get())
        {
            app.RemoveResource<SkinningSourceRegistration>();
        }
        const Detail::SkeletonsHost* host = app.TryGetResource<Detail::SkeletonsHost>();
        if (host != nullptr && host->state.lock() == m_state)
        {
            app.RemoveResource<Detail::SkeletonsHost>();
        }
        app.ForEachScene([](Scene& scene) { scene.RemoveResource<SkeletonPoses>(); });
        m_state.reset();
    }

    PoseStageHandle Skeletons::AddAnimate(std::string name, int order, std::function<void(AnimateContext&)> work)
    {
        if (!m_state)
        {
            return {};
        }
        const std::uint64_t id = m_state->nextId++;
        m_state->animate.Add(
            {id, std::move(name), order, std::make_shared<std::function<void(AnimateContext&)>>(std::move(work))});
        return {id};
    }

    PoseStageHandle Skeletons::AddModifier(std::string name, int order, std::function<void(ModifierContext&)> work)
    {
        if (!m_state)
        {
            return {};
        }
        const std::uint64_t id = m_state->nextId++;
        m_state->modify.Add(
            {id, std::move(name), order, std::make_shared<std::function<void(ModifierContext&)>>(std::move(work))});
        return {id};
    }

    PoseStageHandle Skeletons::AddLate(std::string name, int order, std::function<void(LateContext&)> work)
    {
        if (!m_state)
        {
            return {};
        }
        const std::uint64_t id = m_state->nextId++;
        m_state->late.Add(
            {id, std::move(name), order, std::make_shared<std::function<void(LateContext&)>>(std::move(work))});
        return {id};
    }

    bool Skeletons::Remove(PoseStageHandle handle)
    {
        if (!m_state || handle.id == 0)
        {
            return false;
        }
        if (m_state->running)
        {
            m_state->app->Logger()->Error(
                "{}", "Skeletons::Remove was refused: pose stage work is removed outside the pose pipeline");
            return false;
        }
        return m_state->animate.Remove(handle.id) || m_state->modify.Remove(handle.id) ||
               m_state->late.Remove(handle.id);
    }

    namespace
    {
        // The install `app` holds, through its host resource; null with none.
        std::shared_ptr<Detail::SkeletonsState> HostOf(App& app)
        {
            const Detail::SkeletonsHost* host = app.TryGetResource<Detail::SkeletonsHost>();
            return host == nullptr ? nullptr : host->state.lock();
        }

        template <typename Context>
        PoseStageHandle Host(App& app, Detail::Stage<Context> Detail::SkeletonsState::* stage, std::string name,
                             int order, std::function<void(Context&)> work)
        {
            const std::shared_ptr<Detail::SkeletonsState> state = HostOf(app);
            if (!state)
            {
                return {};
            }
            const std::uint64_t id = state->nextId++;
            ((*state).*stage)
                .Add({id, std::move(name), order, std::make_shared<std::function<void(Context&)>>(std::move(work)),
                      true});
            return {id};
        }
    } // namespace

    PoseStageHandle Skeletons::HostAnimate(App& app, std::string name, int order,
                                           std::function<void(AnimateContext&)> work)
    {
        return Host(app, &Detail::SkeletonsState::animate, std::move(name), order, std::move(work));
    }

    PoseStageHandle Skeletons::HostModifier(App& app, std::string name, int order,
                                            std::function<void(ModifierContext&)> work)
    {
        return Host(app, &Detail::SkeletonsState::modify, std::move(name), order, std::move(work));
    }

    PoseStageHandle Skeletons::HostLate(App& app, std::string name, int order, std::function<void(LateContext&)> work)
    {
        return Host(app, &Detail::SkeletonsState::late, std::move(name), order, std::move(work));
    }

    PoseStageHandle Skeletons::HostPassEnd(App& app, std::function<void(PoseTiming)> work)
    {
        const std::shared_ptr<Detail::SkeletonsState> state = HostOf(app);
        if (!state)
        {
            return {};
        }
        const std::uint64_t id = state->nextId++;
        state->passEnds.emplace_back(id, std::make_shared<std::function<void(PoseTiming)>>(std::move(work)));
        return {id};
    }

    bool Skeletons::RemoveHosted(App& app, PoseStageHandle handle)
    {
        const std::shared_ptr<Detail::SkeletonsState> state = HostOf(app);
        if (!state || handle.id == 0)
        {
            return false;
        }
        if (state->running)
        {
            app.Logger()->Error(
                "{}", "Skeletons::RemoveHosted was refused: pose stage work is removed outside the pose pipeline");
            return false;
        }
        const bool passEnd =
            std::erase_if(state->passEnds, [&](const auto& entry) { return entry.first == handle.id; });
        return passEnd || state->animate.Remove(handle.id) || state->modify.Remove(handle.id) ||
               state->late.Remove(handle.id);
    }

    PoseState Skeletons::Ensure(Scene& scene, Entity skeleton) const
    {
        AssetStore* store = m_state ? m_state->app->TryGetResource<AssetStore>() : nullptr;
        if (store == nullptr)
        {
            return PoseState::Missing;
        }
        return PosesOf(scene, *m_state->app->Logger()).Reconcile(*store, skeleton);
    }

    void Skeletons::SpawnSkeletons(Scene& scene, AssetHandle<Model> model, const SpawnedNodes& spawned) const
    {
        if (m_state)
        {
            SpawnSkeletonsFor(*m_state, scene, model, spawned);
        }
    }

    SpawnedNodes Skeletons::SpawnModel(Scene& scene, AssetHandle<Model> model, Entity parent) const
    {
        AssetStore* store = m_state ? m_state->app->TryGetResource<AssetStore>() : nullptr;
        if (store == nullptr)
        {
            return {};
        }
        SpawnedNodes spawned = store->SpawnNodes(scene, model, parent);
        SpawnSkeletonsFor(*m_state, scene, model, spawned);
        return spawned;
    }

    PoseState Skeletons::GetPoseState(Scene& scene, Entity skeleton)
    {
        SkeletonPoses* poses = scene.GetResource<SkeletonPoses>();
        const SkeletonInstance* instance = poses == nullptr || !skeleton.IsValid() ? nullptr : poses->Find(skeleton);
        if (instance == nullptr)
        {
            return PoseState::Missing;
        }
        return instance->ready ? PoseState::Ready : PoseState::Unavailable;
    }

    std::int32_t Skeletons::FindBone(Scene& scene, Entity skeleton, std::string_view name)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr)
        {
            return NoBone;
        }
        const auto found = instance->bonesByName.find(std::string(name));
        return found == instance->bonesByName.end() ? NoBone : found->second;
    }

    std::span<const Transform> Skeletons::GetLocals(Scene& scene, Entity skeleton)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        return instance == nullptr ? std::span<const Transform>{} : instance->final;
    }

    std::span<const Transform> Skeletons::GetBaseLocals(Scene& scene, Entity skeleton, PoseKind kind)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr)
        {
            return {};
        }
        return ReadsTick(*instance, kind) ? instance->tickBase : instance->base;
    }

    std::span<const Math::Mat4> Skeletons::GetModelMatrices(Scene& scene, Entity skeleton)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        return instance == nullptr ? std::span<const Math::Mat4>{} : SkeletonPoses::AllModels(*instance);
    }

    std::optional<Transform> Skeletons::GetLocal(Scene& scene, Entity skeleton, std::int32_t bone)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return std::nullopt;
        }
        return instance->final[static_cast<std::size_t>(bone)];
    }

    std::optional<Math::Mat4> Skeletons::GetModel(Scene& scene, Entity skeleton, std::int32_t bone)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return std::nullopt;
        }
        return SkeletonPoses::ModelOf(*instance, bone);
    }

    std::optional<Transform> Skeletons::GetBaseLocal(Scene& scene, Entity skeleton, std::int32_t bone, PoseKind kind)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return std::nullopt;
        }
        const std::vector<Transform>& locals = ReadsTick(*instance, kind) ? instance->tickBase : instance->base;
        return locals[static_cast<std::size_t>(bone)];
    }

    std::optional<Math::Mat4> Skeletons::GetBaseModel(Scene& scene, Entity skeleton, std::int32_t bone, PoseKind kind)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return std::nullopt;
        }
        const std::span<const Math::Mat4> models = ReadsTick(*instance, kind) ? SkeletonPoses::AllTickModels(*instance)
                                                                              : SkeletonPoses::AllBaseModels(*instance);
        return models[static_cast<std::size_t>(bone)];
    }

    std::optional<Math::Mat4> Skeletons::GetBoneWorldMatrix(Scene& scene, Entity skeleton, std::int32_t bone,
                                                            PoseKind kind)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return std::nullopt;
        }
        // The simulation pose has no modifiers, so its model matrices are the tick base pose's.
        const Math::Mat4& model = ReadsTick(*instance, kind)
                                      ? SkeletonPoses::AllTickModels(*instance)[static_cast<std::size_t>(bone)]
                                      : SkeletonPoses::ModelOf(*instance, bone);
        return GetWorldMatrix(skeleton) * model;
    }

    std::uint64_t Skeletons::GetPoseGeneration(Scene& scene, Entity skeleton)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        return instance == nullptr ? 0 : instance->generation;
    }

    std::span<const Math::Mat4> Skeletons::GetPalette(Scene& scene, Entity skeleton)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        return instance == nullptr ? std::span<const Math::Mat4>{} : SkeletonPoses::PaletteOf(*instance);
    }

    std::span<Transform> Skeletons::EditBasePose(Scene& scene, Entity skeleton, std::source_location site)
    {
        SkeletonPoses* poses = scene.GetResource<SkeletonPoses>();
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr)
        {
            return {};
        }
        if (poses->step == PipelineStep::Animate && instance->timing != poses->passTiming)
        {
            poses->WarnOncePerSite(site, std::format("FenrirSkeleton: EditBasePose on the {}-posed skeleton entity {} "
                                                     "was refused in Animate's {} pass, which writes only {}-posed "
                                                     "skeletons; it wrote nothing",
                                                     TimingName(instance->timing), Detail::Describe(skeleton),
                                                     TimingName(poses->passTiming), TimingName(poses->passTiming)));
            return {};
        }
        if (poses->step != PipelineStep::None && poses->step != PipelineStep::Reconcile &&
            poses->step != PipelineStep::Animate)
        {
            poses->WarnOncePerSite(site, std::format("FenrirSkeleton: EditBasePose on the skeleton entity {} was "
                                                     "refused in {}: only Animate and code outside the pose pipeline "
                                                     "write the base pose, and modifiers write through PoseEdit; it "
                                                     "wrote nothing",
                                                     Detail::Describe(skeleton), StepName(poses->step)));
            return {};
        }
        instance->poseDirty = true;
        if (instance->timing == PoseTiming::Tick)
        {
            // A tick-posed instance's written base pose is its simulation pose: the next tick evaluates from it, and
            // the frame draws it through the blend.
            instance->tickModelValid = false;
            return instance->tickBase;
        }
        instance->baseModelValid = false;
        return instance->base;
    }

    AttachmentState Skeletons::GetAttachmentState(Scene& scene, Entity entity)
    {
        const SkeletonPoses* poses = scene.GetResource<SkeletonPoses>();
        return poses == nullptr ? AttachmentState::NotAttached : poses->attachments.StateOf(entity);
    }

    DetachResult Skeletons::Detach(Scene& scene, Entity entity, DetachMode mode, Entity newParent)
    {
        SkeletonPoses* poses = scene.GetResource<SkeletonPoses>();
        if (poses != nullptr && poses->step != PipelineStep::None)
        {
            poses->Logger().Error("{}", std::format("Skeletons::Detach on {} was refused: detaching is a structural "
                                                    "change, made outside the pose pipeline",
                                                    Detail::Describe(entity)));
            return DetachResult::InsidePipeline;
        }
        if (!entity.IsValid() || !entity.HasComponent<BoneAttachment>())
        {
            return DetachResult::NotAttached;
        }
        if (mode == DetachMode::KeepWorld)
        {
            // Everything is checked before anything changes.
            if (newParent.HasEntityList() && !newParent.IsValid())
            {
                return DetachResult::InvalidParent;
            }
            for (Entity at = newParent; at.IsValid(); at = at.GetComponent<Relationship>().parent)
            {
                if (at == entity)
                {
                    return DetachResult::InvalidParent;
                }
            }
            const Math::Mat4 above = newParent.IsValid() ? GetWorldMatrix(newParent) : Math::Mat4(1.0f);
            if (!Detail::Invertible(above))
            {
                return DetachResult::ParentNotInvertible;
            }
            const Math::Mat4 local = Math::Inverse(above) * GetWorldMatrix(entity);
            Transform kept;
            Math::Decompose(local, kept.pos, kept.rot, kept.scale);
            kept.rot = glm::normalize(kept.rot);
            const Math::Mat4 rebuilt = TransformToMat4(kept);
            float size = 1.0f;
            float error = 0.0f;
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    size = std::max(size, std::abs(local[column][row]));
                    error = std::max(error, std::abs(local[column][row] - rebuilt[column][row]));
                }
            }
            if (!std::isfinite(error) || error > 1e-4f * size)
            {
                return DetachResult::NotRepresentable;
            }

            entity.RemoveComponent<BoneAttachment>();
            const Entity oldParent = entity.GetComponent<Relationship>().parent;
            if (oldParent.IsValid())
            {
                oldParent.RemoveChild(entity);
            }
            if (newParent.IsValid())
            {
                newParent.AddChild(entity);
            }
            if (entity.HasComponent<Transform>())
            {
                entity.GetComponent<Transform>() = kept;
            }
            else
            {
                entity.AddComponent<Transform>(kept);
            }
        }
        else
        {
            entity.RemoveComponent<BoneAttachment>();
        }
        if (poses != nullptr)
        {
            poses->attachments.Forget(entity);
        }
        return DetachResult::Detached;
    }

    bool Skeletons::SetOverride(Scene& scene, Entity skeleton, std::int32_t bone, const Target& target)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return false;
        }
        std::optional<Target>& entry = instance->overrides[static_cast<std::size_t>(bone)];
        instance->overrideCount += entry ? 0 : 1;
        entry = target;
        return true;
    }

    std::optional<Target> Skeletons::GetOverride(Scene& scene, Entity skeleton, std::int32_t bone)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return std::nullopt;
        }
        return instance->overrides[static_cast<std::size_t>(bone)];
    }

    bool Skeletons::ClearOverride(Scene& scene, Entity skeleton, std::int32_t bone)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone) || !instance->overrides[static_cast<std::size_t>(bone)])
        {
            return false;
        }
        instance->overrides[static_cast<std::size_t>(bone)].reset();
        --instance->overrideCount;
        return true;
    }

    bool Skeletons::ClearOverrides(Scene& scene, Entity skeleton)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr)
        {
            return false;
        }
        instance->overrides.assign(instance->overrides.size(), std::nullopt);
        instance->overrideCount = 0;
        return true;
    }

    bool Skeletons::SetOverrideWeight(Scene& scene, Entity skeleton, std::int32_t bone, float weight,
                                      OverrideScope scope)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return false;
        }
        const float clamped = std::isfinite(weight) ? std::clamp(weight, 0.0f, 1.0f) : 0.0f;
        for (const std::int32_t reached : Reach(*instance, bone, scope))
        {
            instance->overrideWeights[static_cast<std::size_t>(reached)] = clamped;
        }
        return true;
    }

    float Skeletons::GetOverrideWeight(Scene& scene, Entity skeleton, std::int32_t bone)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return 1.0f;
        }
        return instance->overrideWeights[static_cast<std::size_t>(bone)];
    }

    bool Skeletons::ClearOverrideWeight(Scene& scene, Entity skeleton, std::int32_t bone, OverrideScope scope)
    {
        return SetOverrideWeight(scene, skeleton, bone, 1.0f, scope);
    }

    std::vector<std::int32_t> Skeletons::GetSubtree(Scene& scene, Entity skeleton, std::int32_t bone)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return {};
        }
        return Reach(*instance, bone, OverrideScope::BoneSubtree);
    }
} // namespace Fenrir
