#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Device/CommandList.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirShaders/SampledPicture.hpp"
#include "RecordingLogger.hpp"
#include "RendererProbe.hpp"

namespace RendererTest
{
    /** @brief A cube of side 1 centred on the origin: 24 vertices with positions and normals, counter-clockwise. */
    inline Fenrir::MeshData Cube()
    {
        using Fenrir::Math::Vec3;
        Fenrir::MeshData mesh;
        // Each face's normal, and two axes along it whose cross product is the normal.
        const std::array<std::array<Vec3, 3>, 6> faces{{
            {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)},
            {Vec3(-1, 0, 0), Vec3(0, 0, 1), Vec3(0, 1, 0)},
            {Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)},
            {Vec3(0, -1, 0), Vec3(1, 0, 0), Vec3(0, 0, 1)},
            {Vec3(0, 0, 1), Vec3(1, 0, 0), Vec3(0, 1, 0)},
            {Vec3(0, 0, -1), Vec3(0, 1, 0), Vec3(1, 0, 0)},
        }};
        for (const auto& [normal, u, v] : faces)
        {
            const auto first = static_cast<std::uint32_t>(mesh.positions.size());
            const Vec3 centre = normal * 0.5f;
            for (const Vec3 corner : {centre - u * 0.5f - v * 0.5f, centre + u * 0.5f - v * 0.5f,
                                      centre + u * 0.5f + v * 0.5f, centre - u * 0.5f + v * 0.5f})
            {
                mesh.positions.push_back(corner);
                mesh.normals.push_back(normal);
            }
            for (const std::uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u})
            {
                mesh.indices.push_back(first + index);
            }
        }
        return mesh;
    }

    /** @brief A square of side `size` in the XY plane at depth `z`, facing +Z, with positions and normals. */
    inline Fenrir::MeshData Quad(float size = 1.0f, float z = 0.0f)
    {
        using Fenrir::Math::Vec3;
        Fenrir::MeshData mesh;
        const float h = size * 0.5f;
        mesh.positions = {Vec3(-h, -h, z), Vec3(h, -h, z), Vec3(h, h, z), Vec3(-h, h, z)};
        mesh.normals = {Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1)};
        mesh.indices = {0, 1, 2, 0, 2, 3};
        return mesh;
    }

    /** @brief One indexed draw recorded in a pass, with what was bound for it. */
    struct RecordedDraw
    {
        Fenrir::Graphics::PipelineHandle pipeline;
        std::array<Fenrir::Graphics::BindingGroupHandle, 4> groups{};
        std::vector<Fenrir::Graphics::BufferHandle> vertexBuffers;
        Fenrir::Graphics::BufferHandle indexBuffer;
        Fenrir::Graphics::CmdDrawIndexed draw;
    };

    /** @brief Every indexed draw the device received in passes labelled `pass`, in order. */
    inline std::vector<RecordedDraw> DrawsIn(const std::vector<Fenrir::Graphics::CommandList>& lists,
                                             std::string_view pass)
    {
        using namespace Fenrir::Graphics;
        std::vector<RecordedDraw> draws;
        for (const CommandList& list : lists)
        {
            bool inPass = false;
            RecordedDraw bound;
            for (const Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<CmdBeginPass>(&command))
                {
                    inPass = begin->pass.label == pass;
                    bound = {};
                }
                else if (const auto* pipeline = std::get_if<CmdSetPipeline>(&command))
                {
                    bound.pipeline = pipeline->pipeline;
                }
                else if (const auto* group = std::get_if<CmdSetBindingGroup>(&command))
                {
                    bound.groups.at(group->set) = group->group;
                }
                else if (const auto* vertices = std::get_if<CmdSetVertexBuffer>(&command))
                {
                    if (bound.vertexBuffers.size() <= vertices->slot)
                    {
                        bound.vertexBuffers.resize(vertices->slot + 1);
                    }
                    bound.vertexBuffers.at(vertices->slot) = vertices->buffer;
                }
                else if (const auto* indices = std::get_if<CmdSetIndexBuffer>(&command))
                {
                    bound.indexBuffer = indices->buffer;
                }
                else if (const auto* draw = std::get_if<CmdDrawIndexed>(&command))
                {
                    if (inPass)
                    {
                        RecordedDraw recorded = bound;
                        recorded.draw = *draw;
                        draws.push_back(recorded);
                    }
                }
            }
        }
        return draws;
    }

    /** @brief The labels of the passes the device received, in order. */
    inline std::vector<std::string> PassesIn(const std::vector<Fenrir::Graphics::CommandList>& lists)
    {
        std::vector<std::string> labels;
        for (const Fenrir::Graphics::CommandList& list : lists)
        {
            for (const Fenrir::Graphics::Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<Fenrir::Graphics::CmdBeginPass>(&command))
                {
                    labels.push_back(begin->pass.label);
                }
            }
        }
        return labels;
    }

    /** @brief One pass the device received: how it began, and the first viewport, last pipeline and groups set in it.
     */
    struct PassRecord
    {
        Fenrir::Graphics::PassDesc pass;
        Fenrir::Graphics::PixelRect viewport;
        Fenrir::Graphics::PipelineHandle pipeline;
        std::array<Fenrir::Graphics::BindingGroupHandle, 4> groups{};
    };

    /** @brief Every pass the device received, in order. */
    inline std::vector<PassRecord> PassRecordsIn(const std::vector<Fenrir::Graphics::CommandList>& lists)
    {
        using namespace Fenrir::Graphics;
        std::vector<PassRecord> passes;
        for (const CommandList& list : lists)
        {
            bool viewportSet = false;
            for (const Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<CmdBeginPass>(&command))
                {
                    passes.push_back({begin->pass, {}, {}, {}});
                    viewportSet = false;
                }
                else if (passes.empty())
                {
                    continue;
                }
                else if (const auto* viewport = std::get_if<CmdSetViewport>(&command); viewport && !viewportSet)
                {
                    passes.back().viewport = viewport->rect;
                    viewportSet = true;
                }
                else if (const auto* pipeline = std::get_if<CmdSetPipeline>(&command))
                {
                    passes.back().pipeline = pipeline->pipeline;
                }
                else if (const auto* group = std::get_if<CmdSetBindingGroup>(&command))
                {
                    passes.back().groups.at(group->set) = group->group;
                }
            }
        }
        return passes;
    }

    /** @brief The passes labelled `label` among `passes`, in order. */
    inline std::vector<PassRecord> Labelled(const std::vector<PassRecord>& passes, std::string_view label)
    {
        std::vector<PassRecord> found;
        std::copy_if(passes.begin(), passes.end(), std::back_inserter(found),
                     [&](const PassRecord& p) { return p.pass.label == label; });
        return found;
    }

    /** @brief How many of `messages` contain `text`. */
    inline std::size_t CountContaining(const std::vector<std::string>& messages, std::string_view text)
    {
        return static_cast<std::size_t>(std::count_if(
            messages.begin(), messages.end(), [&](const std::string& m) { return m.find(text) != std::string::npos; }));
    }

    /** @brief The float at byte `offset` of `bytes`. */
    inline float FloatAt(const std::vector<std::byte>& bytes, std::size_t offset)
    {
        float value = 0.0f;
        REQUIRE(offset + sizeof(float) <= bytes.size());
        std::memcpy(&value, std::span(bytes).subspan(offset).data(), sizeof(float));
        return value;
    }

    /** @brief The renderer on the null backend in an App with FenrirAssets and no window (the spec's seam 1). */
    inline Fenrir::RendererConfig NullBackend()
    {
        Fenrir::RendererConfig config;
        config.backend = Fenrir::GraphicsBackend::Null;
        return config;
    }

    /**
     * @brief An App with FenrirAssets and the Renderer on the null backend, run one frame at a time, with helpers to
     * spawn what the renderer draws.
     */
    class RendererTestApp
    {
      public:
        explicit RendererTestApp(Fenrir::RendererConfig config = NullBackend())
            : renderer(Fenrir::Renderer::Install(app, config))
        {
        }

        std::shared_ptr<LoggedMessages> messages = std::make_shared<LoggedMessages>();
        Fenrir::App app{std::make_unique<RecordingLogger>(messages), 1};
        Fenrir::Assets assets{Fenrir::Assets::Install(app)};
        Fenrir::Renderer renderer;

        [[nodiscard]] Fenrir::AssetStore& Store()
        {
            return assets.Store();
        }

        [[nodiscard]] Fenrir::Scene& ActiveScene()
        {
            return app.GetActiveScene();
        }

        /** @brief The built-in shader named `name`. */
        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Shader> Builtin(std::string_view name)
        {
            const Fenrir::LoadResult<Fenrir::Shader> loaded =
                Store().Load<Fenrir::Shader>(name, Fenrir::AssetOwner::App());
            INFO("the built-in shader " << name << " loads");
            REQUIRE(loaded.Ok());
            return loaded.handle;
        }

        /** @brief A new material on the built-in shader `name`. */
        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Material> Material(std::string_view name = "builtin:lit")
        {
            return Store().CreateMaterial(Builtin(name), Fenrir::AssetOwner::App());
        }

        /**
         * @brief A material on the test shader SampledPicture, which samples its texture parameter `picture` (white by
         * default), set to `image`.
         */
        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Material> SampledMaterial(Fenrir::AssetHandle<Fenrir::Image> image)
        {
            if (m_sampled.IsEmpty())
            {
                Fenrir::ShaderData data;
                const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::SampledPicture));
                data.bytes.assign(bytes.begin(), bytes.end());
                m_sampled = Store().Add<Fenrir::Shader>(std::move(data), Fenrir::AssetOwner::App());
            }
            const auto material = Store().CreateMaterial(m_sampled, Fenrir::AssetOwner::App());
            INFO("the test material takes the image as its picture");
            REQUIRE(Store().SetTexture(material, "picture", image));
            return material;
        }

        /** @brief A render target: an RGBA8 sRGB image of `width` by `height` with no pixels (ADR-0026). */
        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Image> TargetImage(std::uint32_t width, std::uint32_t height)
        {
            Fenrir::ImageData data;
            data.width = width;
            data.height = height;
            return Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
        }

        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Mesh> Mesh(Fenrir::MeshData data = Cube())
        {
            return Store().Add<Fenrir::Mesh>(std::move(data), Fenrir::AssetOwner::App());
        }

        /** @brief A camera at `position`, looking along -Z. */
        Fenrir::Entity AddCamera(Fenrir::Math::Vec3 position = {0.0f, 0.0f, 5.0f})
        {
            Fenrir::Entity entity = ActiveScene().GetEntityList().CreateEntity();
            entity.GetComponent<Fenrir::Transform>().pos = position;
            entity.AddComponent<Fenrir::Camera>();
            return entity;
        }

        Fenrir::Entity AddMeshRenderer(Fenrir::AssetHandle<Fenrir::Mesh> mesh,
                                       Fenrir::AssetHandle<Fenrir::Material> material)
        {
            Fenrir::Entity entity = ActiveScene().GetEntityList().CreateEntity();
            entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
            return entity;
        }

        /** @brief Every pass of the last frame, in order. */
        [[nodiscard]] std::vector<PassRecord> Passes() const
        {
            return PassRecordsIn(Fenrir::RendererProbe::Submitted(renderer));
        }

        /** @brief Runs one frame of a sixtieth of a second. */
        void Frame()
        {
            app.RunFrame(1.0 / 60.0);
        }

        [[nodiscard]] Fenrir::Graphics::GraphicsDevice& Device() const
        {
            return Fenrir::RendererProbe::Device(renderer);
        }

        /** @brief The indexed draws of the last frame's passes labelled `pass`. */
        [[nodiscard]] std::vector<RecordedDraw> Draws(std::string_view pass = "Opaque") const
        {
            return DrawsIn(Fenrir::RendererProbe::Submitted(renderer), pass);
        }

        /** @brief The label of the binding group a draw bound at `set`. */
        [[nodiscard]] std::string GroupLabel(const RecordedDraw& draw, std::size_t set) const
        {
            return Fenrir::RendererProbe::Label(Device(), draw.groups.at(set));
        }

        /** @brief The bytes of the material block a draw bound at set 2, binding 0. */
        [[nodiscard]] std::vector<std::byte> MaterialBlock(const RecordedDraw& draw) const
        {
            for (const Fenrir::Graphics::BindingGroupEntry& entry :
                 Fenrir::RendererProbe::Entries(Device(), draw.groups.at(2)))
            {
                if (entry.binding == 0)
                {
                    return Fenrir::RendererProbe::Bytes(Device(), entry.buffer);
                }
            }
            return {};
        }

      private:
        Fenrir::AssetHandle<Fenrir::Shader> m_sampled;
    };
} // namespace RendererTest
