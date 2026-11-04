#include "Core/BuiltinShaders.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirShaders/Builtin2D.hpp"
#include "FenrirShaders/BuiltinBackground.hpp"
#include "FenrirShaders/BuiltinBloomDownsample.hpp"
#include "FenrirShaders/BuiltinBloomUpsample.hpp"
#include "FenrirShaders/BuiltinBrdfLut.hpp"
#include "FenrirShaders/BuiltinDebugLines.hpp"
#include "FenrirShaders/BuiltinDebugText.hpp"
#include "FenrirShaders/BuiltinDebugTriangles.hpp"
#include "FenrirShaders/BuiltinDepthPrepass.hpp"
#include "FenrirShaders/BuiltinFallback.hpp"
#include "FenrirShaders/BuiltinGtao.hpp"
#include "FenrirShaders/BuiltinHiZ.hpp"
#include "FenrirShaders/BuiltinLightClusters.hpp"
#include "FenrirShaders/BuiltinLit.hpp"
#include "FenrirShaders/BuiltinOutput.hpp"
#include "FenrirShaders/BuiltinPanorama.hpp"
#include "FenrirShaders/BuiltinSceneHistory.hpp"
#include "FenrirShaders/BuiltinShadow.hpp"
#include "FenrirShaders/BuiltinSkinning.hpp"
#include "FenrirShaders/BuiltinSkyIrradiance.hpp"
#include "FenrirShaders/BuiltinSkySpecular.hpp"
#include "FenrirShaders/BuiltinUnlit.hpp"

namespace Fenrir::Render
{
    namespace
    {
        template <std::size_t N>
        std::span<const std::byte> BytesOf(const std::uint8_t (&embedded)[N])
        {
            return std::as_bytes(std::span<const std::uint8_t, N>(embedded));
        }

        const std::array<BuiltinShader, 22>& Table()
        {
            static const std::array<BuiltinShader, 22> table{{
                {BuiltinProgram::Unlit, "builtin:unlit", BytesOf(Shaders::BuiltinUnlit)},
                {BuiltinProgram::Lit, "builtin:lit", BytesOf(Shaders::BuiltinLit)},
                {BuiltinProgram::Fallback, "builtin:fallback", BytesOf(Shaders::BuiltinFallback)},
                {BuiltinProgram::Background, "builtin:background", BytesOf(Shaders::BuiltinBackground)},
                {BuiltinProgram::Output, "builtin:output", BytesOf(Shaders::BuiltinOutput)},
                {BuiltinProgram::DebugLines, "builtin:debug-lines", BytesOf(Shaders::BuiltinDebugLines)},
                {BuiltinProgram::DebugTriangles, "builtin:debug-triangles", BytesOf(Shaders::BuiltinDebugTriangles)},
                {BuiltinProgram::DebugText, "builtin:debug-text", BytesOf(Shaders::BuiltinDebugText)},
                {BuiltinProgram::Draw2D, "builtin:2d", BytesOf(Shaders::Builtin2D)},
                {BuiltinProgram::Panorama, "builtin:panorama", BytesOf(Shaders::BuiltinPanorama)},
                {BuiltinProgram::SkyIrradiance, "builtin:sky-irradiance", BytesOf(Shaders::BuiltinSkyIrradiance)},
                {BuiltinProgram::SkySpecular, "builtin:sky-specular", BytesOf(Shaders::BuiltinSkySpecular)},
                {BuiltinProgram::BrdfLut, "builtin:brdf-lut", BytesOf(Shaders::BuiltinBrdfLut)},
                {BuiltinProgram::BloomDownsample, "builtin:bloom-downsample", BytesOf(Shaders::BuiltinBloomDownsample)},
                {BuiltinProgram::BloomUpsample, "builtin:bloom-upsample", BytesOf(Shaders::BuiltinBloomUpsample)},
                {BuiltinProgram::DepthPrepass, "builtin:depth-prepass", BytesOf(Shaders::BuiltinDepthPrepass)},
                {BuiltinProgram::Gtao, "builtin:gtao", BytesOf(Shaders::BuiltinGtao)},
                {BuiltinProgram::Skinning, "builtin:skinning", BytesOf(Shaders::BuiltinSkinning)},
                {BuiltinProgram::Shadow, "builtin:shadow", BytesOf(Shaders::BuiltinShadow)},
                {BuiltinProgram::LightClusters, "builtin:light-clusters", BytesOf(Shaders::BuiltinLightClusters)},
                {BuiltinProgram::HiZ, "builtin:hi-z", BytesOf(Shaders::BuiltinHiZ)},
                {BuiltinProgram::SceneHistory, "builtin:scene-history", BytesOf(Shaders::BuiltinSceneHistory)},
            }};
            return table;
        }
    } // namespace

    std::span<const BuiltinShader> BuiltinShaders()
    {
        return Table();
    }

    const BuiltinShader& GetBuiltinShader(BuiltinProgram program)
    {
        return Table().at(static_cast<std::size_t>(program));
    }

    ShaderContainer ReadBuiltin(BuiltinProgram program, ILogger& logger)
    {
        ShaderContainerResult read = ReadShaderContainer(GetBuiltinShader(program).bytes);
        if (!read.Ok())
        {
            logger.Error("FenrirRenderer: the built-in shader '{}' does not read: {}", GetBuiltinShader(program).name,
                         read.diagnostic);
            return {};
        }
        return std::move(*read.container);
    }

    Graphics::PipelineHandle MakeBuiltinPipeline(Graphics::GraphicsDevice& device, ILogger& logger,
                                                 const Graphics::PipelineDesc& desc, std::string_view name)
    {
        const Graphics::PipelineResult made = device.CreatePipeline(desc, name);
        if (made.pipeline.IsEmpty())
        {
            logger.Error("FenrirRenderer: the built-in pipeline '{}' could not be made: {}", name, made.diagnostic);
        }
        return made.pipeline;
    }
} // namespace Fenrir::Render
