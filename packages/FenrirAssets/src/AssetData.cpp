#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/ShaderData.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // Empty when the stream is absent or has one value per position, else what is wrong with it.
        template <typename Stream>
        std::string CheckStream(std::string_view name, const Stream& stream, std::size_t vertices)
        {
            if (stream.empty() || stream.size() == vertices)
            {
                return {};
            }
            return std::format("its {} stream has {} values for {} positions", name, stream.size(), vertices);
        }
    } // namespace

    std::string PrepareAsset(MeshData& mesh)
    {
        const std::size_t vertices = mesh.positions.size();
        for (const std::string& problem :
             {CheckStream("normals", mesh.normals, vertices), CheckStream("tangents", mesh.tangents, vertices),
              CheckStream("uv0", mesh.uv0, vertices), CheckStream("uv1", mesh.uv1, vertices),
              CheckStream("colours", mesh.colours, vertices)})
        {
            if (!problem.empty())
            {
                return problem;
            }
        }
        if (mesh.indices.size() % 3 != 0)
        {
            return std::format("its {} indices are not whole triangles", mesh.indices.size());
        }
        for (std::size_t i = 0; i < mesh.indices.size(); ++i)
        {
            if (mesh.indices[i] >= vertices)
            {
                return std::format("index {} is {}, past its {} vertices", i, mesh.indices[i], vertices);
            }
        }
        for (std::size_t i = 0; i < mesh.submeshes.size(); ++i)
        {
            const Submesh& submesh = mesh.submeshes[i];
            if (std::size_t{submesh.indexOffset} + submesh.indexCount > mesh.indices.size())
            {
                return std::format("submesh {} reaches index {}, past its {} indices", i,
                                   std::size_t{submesh.indexOffset} + submesh.indexCount, mesh.indices.size());
            }
        }

        mesh.bounds = {Math::Vec3(0.0f), Math::Vec3(0.0f)};
        if (!mesh.positions.empty())
        {
            mesh.bounds = {mesh.positions.front(), mesh.positions.front()};
            for (const Math::Vec3& position : mesh.positions)
            {
                mesh.bounds.min = glm::min(mesh.bounds.min, position);
                mesh.bounds.max = glm::max(mesh.bounds.max, position);
            }
        }
        return {};
    }

    std::string PrepareAsset(ImageData& image)
    {
        if (image.width == 0 || image.height == 0)
        {
            return std::format("it is {} by {} pixels, and an image needs at least one", image.width, image.height);
        }
        const bool cubemap = image.shape == ImageShape::Cubemap;
        if (cubemap && image.width != image.height)
        {
            return std::format("its cubemap faces are {} by {} pixels, and faces must be square", image.width,
                               image.height);
        }
        if (image.pixels.empty())
        {
            return {};
        }
        const std::size_t faces = cubemap ? 6 : 1;
        const std::size_t expected = std::size_t{image.width} * image.height * BytesPerPixel(image.format) * faces;
        if (image.pixels.size() != expected)
        {
            return std::format("it has {} bytes of pixels where {} by {}{} needs {}", image.pixels.size(), image.width,
                               image.height, cubemap ? " by six faces" : "", expected);
        }
        return {};
    }

    std::string PrepareAsset(ShaderData& shader)
    {
        ShaderContainerResult read = ReadShaderContainer(shader.bytes);
        if (!read.Ok())
        {
            return std::move(read.diagnostic);
        }
        shader.parameters = CollectShaderParameters(*read.container);
        return {};
    }

    std::string PrepareAsset(MaterialData&)
    {
        // Any material is usable: names and types are checked against its shader when they are set, and a stale
        // shader handle is the renderer's to report (it draws the fallback material).
        return {};
    }
} // namespace Fenrir::Detail
