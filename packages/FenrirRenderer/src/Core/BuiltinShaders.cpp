#include "Core/BuiltinShaders.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "FenrirShaders/BuiltinBackground.hpp"
#include "FenrirShaders/BuiltinDebugLines.hpp"
#include "FenrirShaders/BuiltinDebugText.hpp"
#include "FenrirShaders/BuiltinDebugTriangles.hpp"
#include "FenrirShaders/BuiltinFallback.hpp"
#include "FenrirShaders/BuiltinLit.hpp"
#include "FenrirShaders/BuiltinOutput.hpp"
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

        const std::array<BuiltinShader, 8>& Table()
        {
            static const std::array<BuiltinShader, 8> table{{
                {BuiltinProgram::Unlit, "builtin:unlit", BytesOf(Shaders::BuiltinUnlit)},
                {BuiltinProgram::Lit, "builtin:lit", BytesOf(Shaders::BuiltinLit)},
                {BuiltinProgram::Fallback, "builtin:fallback", BytesOf(Shaders::BuiltinFallback)},
                {BuiltinProgram::Background, "builtin:background", BytesOf(Shaders::BuiltinBackground)},
                {BuiltinProgram::Output, "builtin:output", BytesOf(Shaders::BuiltinOutput)},
                {BuiltinProgram::DebugLines, "builtin:debug-lines", BytesOf(Shaders::BuiltinDebugLines)},
                {BuiltinProgram::DebugTriangles, "builtin:debug-triangles", BytesOf(Shaders::BuiltinDebugTriangles)},
                {BuiltinProgram::DebugText, "builtin:debug-text", BytesOf(Shaders::BuiltinDebugText)},
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
} // namespace Fenrir::Render
