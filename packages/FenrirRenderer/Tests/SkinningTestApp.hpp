#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "RendererTestApp.hpp"

namespace RendererTest
{
    /**
     * @brief The two-bone rig the skinning tests draw: `lower` at the skeleton entity's origin and `upper` one unit
     * above it, each inverse bind the inverse of its rest model matrix, so the palette at rest is the identity.
     */
    inline Fenrir::SkeletonData TwoBones()
    {
        Fenrir::SkeletonData skeleton;
        skeleton.bones.resize(2);
        skeleton.bones[0].sourceName = "lower";
        skeleton.bones[1].sourceName = "upper";
        skeleton.bones[1].parent = 0;
        skeleton.bones[1].rest.pos = {0.0f, 1.0f, 0.0f};
        skeleton.bones[1].inverseBind = Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), {0.0f, -1.0f, 0.0f});
        return skeleton;
    }

    /**
     * @brief A strip one unit wide and two tall in the XY plane, facing +Z, in TwoBones' bind space: its bottom and
     * middle rows weighted wholly to `lower`, its top row (y = 2) wholly to `upper`.
     */
    inline Fenrir::MeshData Strip()
    {
        using Fenrir::Math::Vec3;
        Fenrir::MeshData mesh;
        Fenrir::MeshSkin skin;
        for (int row = 0; row < 3; ++row)
        {
            for (const float x : {-0.5f, 0.5f})
            {
                mesh.positions.emplace_back(x, static_cast<float>(row), 0.0f);
                mesh.normals.emplace_back(0.0f, 0.0f, 1.0f);
                skin.joints.push_back({row == 2 ? std::uint16_t{1} : std::uint16_t{0}, 0, 0, 0});
                skin.weights.push_back({std::uint16_t{65535}, 0, 0, 0});
            }
        }
        mesh.indices = {0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4};
        mesh.skin = std::move(skin);
        return mesh;
    }

    /** @brief A draw's record in the draw buffer (set 3, binding 0), as `Fenrir/Draw.glsl` lays it out. */
    struct DrawRecord
    {
        Fenrir::Math::Mat4 world{1.0f};
        std::uint32_t paletteOffset = 0;
        float worldSign = 1.0f;
    };

    /** @brief One palette entry (set 3, binding 1): rows 0-2 of a skinning matrix. */
    struct PaletteRows
    {
        Fenrir::Math::Vec4 r0{0.0f};
        Fenrir::Math::Vec4 r1{0.0f};
        Fenrir::Math::Vec4 r2{0.0f};
    };

    /** @brief A skeleton entity and a mesh entity drawing the strip from it. */
    struct SkinnedRig
    {
        Fenrir::Entity owner;
        Fenrir::Entity mesh;
    };

    /**
     * @brief RendererTestApp with FenrirSkeleton installed too, so the renderer finds a skinning source, unless
     * `withSkeletons` is false. Its skeleton owner is destroyed before the renderer's.
     */
    class SkinningTestApp : public RendererTestApp
    {
      public:
        explicit SkinningTestApp(bool withSkeletons = true)
        {
            if (withSkeletons)
            {
                skeletons.emplace(Fenrir::Skeletons::Install(app));
            }
        }

        std::optional<Fenrir::Skeletons> skeletons;

        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Skeleton> Skeleton()
        {
            if (m_skeleton.IsEmpty())
            {
                m_skeleton = Store().Add<Fenrir::Skeleton>(TwoBones(), Fenrir::AssetOwner::App());
            }
            return m_skeleton;
        }

        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Mesh> StripMesh()
        {
            if (m_strip.IsEmpty())
            {
                m_strip = Mesh(Strip());
            }
            return m_strip;
        }

        /** @brief A skeleton entity posing TwoBones at `position`. */
        Fenrir::Entity AddSkeleton(Fenrir::Math::Vec3 position = {0.0f, 0.0f, 0.0f})
        {
            Fenrir::Entity owner = ActiveScene().GetEntityList().CreateEntity();
            owner.GetComponent<Fenrir::Transform>().pos = position;
            owner.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{Skeleton()});
            return owner;
        }

        /** @brief A mesh entity drawing `mesh` with `material`, skinned by `skeleton`. */
        Fenrir::Entity AddSkinned(Fenrir::Entity skeleton, Fenrir::AssetHandle<Fenrir::Mesh> mesh,
                                  Fenrir::AssetHandle<Fenrir::Material> material)
        {
            Fenrir::Entity entity = AddMeshRenderer(mesh, material);
            entity.AddComponent<Fenrir::SkinnedMesh>(Fenrir::SkinnedMesh{skeleton});
            return entity;
        }

        /** @brief A skeleton entity at `position` and a strip on builtin:lit skinned by it. */
        SkinnedRig AddRig(Fenrir::Math::Vec3 position = {0.0f, 0.0f, 0.0f})
        {
            const Fenrir::Entity owner = AddSkeleton(position);
            return {owner, AddSkinned(owner, StripMesh(), Material())};
        }

        /** @brief A draw's record of the draw buffer. */
        [[nodiscard]] DrawRecord Record(const RecordedDraw& draw) const
        {
            const std::vector<std::byte> bytes = BindingBytes(draw, 0);
            const std::size_t at = std::size_t{draw.draw.firstInstance} * RecordSize;
            DrawRecord record;
            REQUIRE(at + RecordSize <= bytes.size());
            std::memcpy(&record.world, std::span(bytes).subspan(at).data(), sizeof(record.world));
            std::memcpy(&record.paletteOffset, std::span(bytes).subspan(at + 128).data(), sizeof(std::uint32_t));
            record.worldSign = FloatAt(bytes, at + 132);
            return record;
        }

        /** @brief Palette entry `bone` of the palette buffer a draw bound. */
        [[nodiscard]] PaletteRows Palette(const RecordedDraw& draw, std::size_t bone) const
        {
            const std::vector<std::byte> bytes = BindingBytes(draw, 1);
            PaletteRows rows;
            REQUIRE((bone + 1) * sizeof(PaletteRows) <= bytes.size());
            std::memcpy(&rows, std::span(bytes).subspan(bone * sizeof(PaletteRows)).data(), sizeof(PaletteRows));
            return rows;
        }

        /** @brief The bytes of the draw set's binding `binding` a draw bound. */
        [[nodiscard]] std::vector<std::byte> BindingBytes(const RecordedDraw& draw, std::uint32_t binding) const
        {
            for (const Fenrir::Graphics::BindingGroupEntry& entry :
                 Fenrir::RendererProbe::Entries(Device(), draw.groups.at(3)))
            {
                if (entry.binding == binding)
                {
                    return Fenrir::RendererProbe::Bytes(Device(), entry.buffer);
                }
            }
            return {};
        }

        /** @brief The bytes of one record of the draw buffer (`Fenrir/Draw.glsl`'s `DrawItem`). */
        static constexpr std::size_t RecordSize = 144;

      private:
        Fenrir::AssetHandle<Fenrir::Skeleton> m_skeleton;
        Fenrir::AssetHandle<Fenrir::Mesh> m_strip;
    };
} // namespace RendererTest
