#include "SkeletonPoses.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirScene/Scene.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // A degenerate transform held this many frames is warned about again, about every five seconds at 60 Hz.
        constexpr std::uint64_t WarningInterval = 300;

        std::size_t Index(std::int32_t bone)
        {
            return static_cast<std::size_t>(bone);
        }
    } // namespace

    bool Invertible(const Math::Mat4& matrix)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!std::isfinite(matrix[column][row]))
                {
                    return false;
                }
            }
        }
        float longest = 0.0f;
        for (int column = 0; column < 3; ++column)
        {
            longest = std::max(longest, glm::length(Math::Vec3(matrix[column])));
        }
        const float determinant = glm::determinant(Math::Mat3(matrix));
        // Relative to the matrix's size, as ADR-0035's skinning test is, so a small but well-shaped scale still
        // inverts.
        return longest > 0.0f && std::abs(determinant) >= 1e-6f * longest * longest * longest;
    }

    std::string Describe(Entity entity)
    {
        if (entity.IsValid() && entity.HasComponent<Name>() && entity.GetComponent<Name>().Get()[0] != '\0')
        {
            return std::format("'{}'", entity.GetComponent<Name>().Get());
        }
        return std::format("entity {}", entity.GetId());
    }

    SkeletonInstance* SkeletonPoses::Find(Entity entity)
    {
        const auto found = m_instances.find(entity.GetId());
        return found == m_instances.end() || found->second.entity != entity ? nullptr : &found->second;
    }

    SkeletonInstance* SkeletonPoses::FindReady(Entity entity)
    {
        SkeletonInstance* instance = Find(entity);
        return instance != nullptr && instance->ready ? instance : nullptr;
    }

    PoseState SkeletonPoses::Reconcile(const AssetStore& store, Entity entity)
    {
        if (!entity.IsValid() || !entity.HasComponent<SkeletonPose>())
        {
            m_instances.erase(entity.GetId());
            return PoseState::Missing;
        }
        const SkeletonPose& pose = entity.GetComponent<SkeletonPose>();
        auto [found, made] = m_instances.try_emplace(entity.GetId());
        SkeletonInstance& instance = found->second;
        if (made || instance.entity != entity)
        {
            // A new component, or a recycled id: a fresh instance either way.
            instance = SkeletonInstance{};
            instance.entity = entity;
        }
        instance.timing = pose.timing;

        const SkeletonData* data = store.TryGet(pose.skeleton);
        if (data == nullptr)
        {
            MakeUnavailable(instance, pose.skeleton);
            return PoseState::Unavailable;
        }
        const std::uint32_t revision = store.Revision(pose.skeleton);
        if (!instance.ready || instance.skeleton != pose.skeleton || instance.revision != revision)
        {
            Rebuild(instance, *data, pose.skeleton, revision);
        }
        return PoseState::Ready;
    }

    void SkeletonPoses::ReconcileAll(const AssetStore& store, Scene& scene)
    {
        EntityList& entities = scene.GetEntityList();
        std::vector<Entity> skeletons;
        const std::array<ComponentType, 1> poses{ComponentType::Native<SkeletonPose>()};
        entities.ForEachWith(poses, [&](std::uint32_t id) { skeletons.push_back(entities.GetEntity(id)); });
        for (const Entity& skeleton : skeletons)
        {
            Reconcile(store, skeleton);
        }
        std::erase_if(m_instances, [](const auto& entry) {
            const Entity& entity = entry.second.entity;
            return !entity.IsValid() || !entity.HasComponent<SkeletonPose>();
        });
    }

    std::vector<Entity> SkeletonPoses::ReadyEntities(std::optional<PoseTiming> timing) const
    {
        std::vector<Entity> ready;
        for (const auto& [id, instance] : m_instances)
        {
            if (instance.ready && (!timing.has_value() || instance.timing == *timing))
            {
                ready.push_back(instance.entity);
            }
        }
        std::ranges::sort(ready, {}, [](const Entity& entity) { return entity.GetId(); });
        return ready;
    }

    void SkeletonPoses::Rebuild(SkeletonInstance& instance, const SkeletonData& data, AssetHandle<Skeleton> handle,
                                std::uint32_t revision)
    {
        const std::size_t count = data.BoneCount();
        instance.skeleton = handle;
        instance.revision = revision;
        instance.ready = true;
        instance.reportedUnavailable = false;
        instance.parents.resize(count);
        instance.inverseBinds.resize(count);
        instance.base.resize(count);
        instance.bonesByName.clear();
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto bone = static_cast<std::int32_t>(i);
            instance.parents[i] = data.bones[i].parent;
            instance.inverseBinds[i] = data.bones[i].inverseBind;
            instance.base[i] = data.bones[i].rest;
            instance.bonesByName.emplace(std::string(data.EngineName(bone)), bone);
        }
        instance.final = instance.base;
        instance.overrides.assign(count, std::nullopt);
        instance.overrideWeights.assign(count, 1.0f);
        instance.overrideCount = 0;
        instance.model.assign(count, Math::Mat4(1.0f));
        instance.modelValid.assign(count, 0);
        AllModels(instance);
        instance.baseModel.clear();
        instance.baseModelValid = false;
        instance.palette.clear();
        instance.paletteGeneration = 0;
        instance.poseDirty = false;
        instance.prepared = false;
        instance.modifiedThisFrame = false;
        instance.modifiedLastFrame = false;
        instance.worldInverse.reset();
        instance.worldInverseFrame = 0;
        instance.generation = m_nextGeneration++;
        instance.built = instance.generation;
    }

    void SkeletonPoses::MakeUnavailable(SkeletonInstance& instance, AssetHandle<Skeleton> handle)
    {
        const bool wasReady = instance.ready;
        instance = SkeletonInstance{.entity = instance.entity,
                                    .skeleton = handle,
                                    .timing = instance.timing,
                                    .reportedUnavailable = instance.reportedUnavailable && !wasReady};
        if (!instance.reportedUnavailable)
        {
            instance.reportedUnavailable = true;
            m_logger->Warn("{}", std::format("FenrirSkeleton: the skeleton entity {} names {} skeleton, so its pose is "
                                             "unavailable: reads are empty until its SkeletonPose names a loaded one",
                                             Describe(instance.entity), handle.IsEmpty() ? "no" : "a freed (stale)"));
        }
    }

    void SkeletonPoses::Prepare(SkeletonInstance& instance)
    {
        instance.final = instance.base;
        std::ranges::fill(instance.modelValid, std::uint8_t{0});
        instance.prepared = true;
    }

    void SkeletonPoses::Finalise(SkeletonInstance& instance)
    {
        AllModels(instance);
        instance.generation = m_nextGeneration++;
        instance.modifiedLastFrame = instance.modifiedThisFrame;
        instance.modifiedThisFrame = false;
        instance.poseDirty = false;
        instance.prepared = false;
    }

    const Math::Mat4& SkeletonPoses::ModelOf(SkeletonInstance& instance, std::int32_t bone)
    {
        if (instance.modelValid[Index(bone)] == 0)
        {
            // Climb to the highest stale ancestor, then compute back down, parents first.
            std::vector<std::int32_t> chain;
            for (std::int32_t at = bone; at != NoBone && instance.modelValid[Index(at)] == 0;
                 at = instance.parents[Index(at)])
            {
                chain.push_back(at);
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                const std::int32_t parent = instance.parents[Index(*it)];
                const Math::Mat4 local = TransformToMat4(instance.final[Index(*it)]);
                instance.model[Index(*it)] = parent == NoBone ? local : instance.model[Index(parent)] * local;
                instance.modelValid[Index(*it)] = 1;
            }
        }
        return instance.model[Index(bone)];
    }

    std::span<const Math::Mat4> SkeletonPoses::AllModels(SkeletonInstance& instance)
    {
        for (std::size_t i = 0; i < instance.model.size(); ++i)
        {
            if (instance.modelValid[i] == 0)
            {
                const std::int32_t parent = instance.parents[i];
                const Math::Mat4 local = TransformToMat4(instance.final[i]);
                // Parents come first, so a parent is valid by the time its child is reached.
                instance.model[i] = parent == NoBone ? local : instance.model[Index(parent)] * local;
                instance.modelValid[i] = 1;
            }
        }
        return instance.model;
    }

    std::span<const Math::Mat4> SkeletonPoses::AllBaseModels(SkeletonInstance& instance)
    {
        if (!instance.baseModelValid)
        {
            instance.baseModel.resize(instance.base.size());
            for (std::size_t i = 0; i < instance.base.size(); ++i)
            {
                const std::int32_t parent = instance.parents[i];
                const Math::Mat4 local = TransformToMat4(instance.base[i]);
                instance.baseModel[i] = parent == NoBone ? local : instance.baseModel[Index(parent)] * local;
            }
            instance.baseModelValid = true;
        }
        return instance.baseModel;
    }

    std::span<const Math::Mat4> SkeletonPoses::PaletteOf(SkeletonInstance& instance)
    {
        if (instance.paletteGeneration != instance.generation)
        {
            const std::span<const Math::Mat4> models = AllModels(instance);
            instance.palette.resize(models.size());
            for (std::size_t i = 0; i < models.size(); ++i)
            {
                instance.palette[i] = models[i] * instance.inverseBinds[i];
            }
            instance.paletteGeneration = instance.generation;
        }
        return instance.palette;
    }

    void SkeletonPoses::Invalidate(SkeletonInstance& instance, std::int32_t bone)
    {
        instance.modelValid[Index(bone)] = 0;
        for (std::size_t i = Index(bone) + 1; i < instance.modelValid.size(); ++i)
        {
            const std::int32_t parent = instance.parents[i];
            if (parent != NoBone && instance.modelValid[Index(parent)] == 0)
            {
                instance.modelValid[i] = 0;
            }
        }
    }

    void SkeletonPoses::WarnOncePerSite(std::source_location site, std::string_view message)
    {
        std::string key = std::format("{}:{}:{}", site.file_name(), site.line(), site.column());
        if (m_warnedSites.insert(std::move(key)).second)
        {
            m_logger->Warn("{}", std::format("{} (at {}:{}; logged once for this call site)", message, site.file_name(),
                                             site.line()));
        }
    }

    void SkeletonPoses::WarnRateLimited(Entity entity, std::int32_t bone, std::string_view message)
    {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(entity.GetId()) << 32U) | static_cast<std::uint32_t>(bone);
        const auto [found, first] = m_lastWarnedFrame.try_emplace(key, frame);
        if (first || frame - found->second >= WarningInterval)
        {
            found->second = frame;
            m_logger->Warn("{}", message);
        }
    }
} // namespace Fenrir::Detail
