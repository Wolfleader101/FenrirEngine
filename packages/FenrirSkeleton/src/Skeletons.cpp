#include "FenrirSkeleton/Skeletons.hpp"

#include <cstddef>
#include <format>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Model.hpp"
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

    // The pose pipeline's one frame pass on the active scene; a friend of the stage contexts, which only it makes.
    struct Detail::PosePipeline
    {
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

            // Bone attachments are placed here once they exist (a later ticket's).
            poses.step = PipelineStep::Attach;

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
        app.AddSequentialSystem(SchedulePriority::PostUpdate,
                                [installed = std::weak_ptr<Detail::SkeletonsState>(state)](App& running) {
                                    if (const std::shared_ptr<Detail::SkeletonsState> current = installed.lock())
                                    {
                                        Detail::PosePipeline::Run(*current, running);
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

    std::span<const Transform> Skeletons::GetBaseLocals(Scene& scene, Entity skeleton, PoseKind)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        return instance == nullptr ? std::span<const Transform>{} : instance->base;
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

    std::optional<Transform> Skeletons::GetBaseLocal(Scene& scene, Entity skeleton, std::int32_t bone, PoseKind)
    {
        const SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return std::nullopt;
        }
        return instance->base[static_cast<std::size_t>(bone)];
    }

    std::optional<Math::Mat4> Skeletons::GetBaseModel(Scene& scene, Entity skeleton, std::int32_t bone, PoseKind)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return std::nullopt;
        }
        return SkeletonPoses::AllBaseModels(*instance)[static_cast<std::size_t>(bone)];
    }

    std::optional<Math::Mat4> Skeletons::GetBoneWorldMatrix(Scene& scene, Entity skeleton, std::int32_t bone, PoseKind)
    {
        SkeletonInstance* instance = ReadyIn(scene, skeleton);
        if (instance == nullptr || !InRange(*instance, bone))
        {
            return std::nullopt;
        }
        return GetWorldMatrix(skeleton) * SkeletonPoses::ModelOf(*instance, bone);
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
        instance->baseModelValid = false;
        return instance->base;
    }
} // namespace Fenrir
