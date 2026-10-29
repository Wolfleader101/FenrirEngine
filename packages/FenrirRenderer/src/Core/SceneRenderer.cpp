#include "Core/SceneRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <string_view>
#include <type_traits>
#include <variant>

#include <glm/gtc/packing.hpp>

#include "Core/BuiltinShaders.hpp"
#include "Core/Skinning.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief The frame block, as `Fenrir/Frame.glsl` declares it (std140). */
        struct FrameBlock
        {
            Math::Vec4 lightDirection;
            Math::Vec4 lightColour;
            Math::Vec4 ambient;
            Math::Vec4 time;
        };
        static_assert(sizeof(FrameBlock) == 64);

        /** @brief The view block, as `Fenrir/View.glsl` declares it (std140). */
        struct ViewBlock
        {
            Math::Mat4 view;
            Math::Mat4 projection;
            Math::Mat4 viewProjection;
            Math::Mat4 inverseViewProjection;
            Math::Vec4 cameraPosition;
            Math::Vec4 viewport;
        };
        static_assert(sizeof(ViewBlock) == 288);

        /** @brief One draw record, as `Fenrir/Draw.glsl` declares it (std430). */
        struct DrawBlock
        {
            Math::Mat4 world;
            Math::Mat4 normal;
            /** @brief A skinned item's first palette entry, in bones. */
            std::uint32_t paletteOffset = 0;
            /** @brief The sign of the world matrix's determinant: -1 when it mirrors, else 1. */
            float worldSign = 1.0f;
            std::uint32_t padding0 = 0;
            std::uint32_t padding1 = 0;
        };
        static_assert(sizeof(DrawBlock) == 144);
        static_assert(offsetof(DrawBlock, paletteOffset) == 128 && offsetof(DrawBlock, worldSign) == 132);

        /** @brief The background block, as `builtin:background` declares it (std140). */
        struct BackgroundBlock
        {
            Math::Vec4 colour;
        };

        /** @brief One binding of a fixed set as the renderer declares it: a block's kind and size (0: any size). */
        struct FixedBinding
        {
            std::uint32_t set = 0;
            std::uint32_t binding = 0;
            ShaderBindingKind kind = ShaderBindingKind::UniformBuffer;
            std::uint32_t size = 0;
        };

        // Sets 0, 1 and 3, declared in C++ (ADR-0024, ADR-0035). The draw and palette buffers hold as many records as
        // they need; reflection sizes such a block by one record, so their record layouts are checked.
        [[maybe_unused]] constexpr std::array<FixedBinding, 4> FixedBindings{{
            {0, 0, ShaderBindingKind::UniformBuffer, sizeof(FrameBlock)},
            {1, 0, ShaderBindingKind::UniformBuffer, sizeof(ViewBlock)},
            {3, 0, ShaderBindingKind::StorageBuffer, sizeof(DrawBlock)},
            {3, 1, ShaderBindingKind::StorageBuffer, sizeof(GpuBone)},
        }};

        /** @brief The binding set of the material. */
        constexpr std::uint32_t MaterialSet = 2;

        /** @brief One standard vertex stream (`Fenrir/Vertex.glsl`), by location: its name, format and size. */
        struct StreamFormat
        {
            std::string_view name;
            VertexFormat format = VertexFormat::Float3;
            std::size_t size = 0;
            /** @brief Where its constant default sits in the default stream buffer. */
            std::size_t defaultOffset = 0;
            std::string_view defaultName;
        };

        // Joints and weights (ADR-0035) are a skinned mesh's; their zero defaults weigh nothing, the identity.
        constexpr std::array<StreamFormat, 8> Streams{{
            {"positions", VertexFormat::Float3, 12, 0, "the origin"},
            {"normals", VertexFormat::Float3, 12, 12, "normal +Y"},
            {"tangents", VertexFormat::Float4, 16, 24, "tangent +X"},
            {"uv0", VertexFormat::Float2, 8, 40, "UV 0"},
            {"uv1", VertexFormat::Float2, 8, 48, "UV 0"},
            {"colours", VertexFormat::Float4, 16, 56, "white"},
            {"joints", VertexFormat::Uint16x4, 8, 72, "bone 0"},
            {"weights", VertexFormat::Unorm16x4, 8, 80, "no weight"},
        }};

        // The constants above, back to back: position, normal, tangent, UV0, UV1, colour, then the joints' and weights'
        // zero bits.
        constexpr std::array<float, 22> DefaultStreams{0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
                                                       0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                                                       1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};

        constexpr std::uint8_t OpaqueStage = static_cast<std::uint8_t>(RenderStage::Opaque);
        constexpr std::uint8_t TransparentStage = static_cast<std::uint8_t>(RenderStage::Transparent);

        /** @brief The variant key the renderer sets for a `Masked` material (ADR-0024). */
        constexpr std::string_view AlphaMaskKey = "ALPHA_MASK";
        /** @brief The variant key the renderer sets for a skinned draw (ADR-0035). */
        constexpr std::string_view SkinnedKey = "SKINNED";
        /** @brief The block member a `Masked` material's alpha cutoff is written into. */
        constexpr std::string_view AlphaCutoffMember = "alphaCutoff";

        /** @brief Whether a material of `mode` draws in the Transparent pass, blended and without writing depth. */
        constexpr bool IsTransparent(Fenrir::BlendMode mode) noexcept
        {
            return mode == Fenrir::BlendMode::AlphaBlend || mode == Fenrir::BlendMode::Additive;
        }

        /** @brief How the device blends a material of `mode`. */
        constexpr Graphics::BlendMode DeviceBlendOf(Fenrir::BlendMode mode) noexcept
        {
            if (mode == Fenrir::BlendMode::AlphaBlend)
            {
                return Graphics::BlendMode::Alpha;
            }
            return mode == Fenrir::BlendMode::Additive ? Graphics::BlendMode::Additive : Graphics::BlendMode::None;
        }

        /** @brief How many mip levels a full chain for an image of `width` x `height` has, down to 1 x 1. */
        std::uint32_t MipCount(std::uint32_t width, std::uint32_t height)
        {
            std::uint32_t levels = 1;
            for (std::uint32_t side = std::max(width, height); side > 1; side /= 2)
            {
                ++levels;
            }
            return levels;
        }

        /** @brief The device format an image is uploaded as: its own format, sRGB-decoded on sampling if it is sRGB. */
        TextureFormat TextureFormatOf(const ImageData& image)
        {
            if (image.format == ImageFormat::RGBA16F)
            {
                return TextureFormat::Rgba16Float;
            }
            if (image.format == ImageFormat::RGBA32F)
            {
                return TextureFormat::Rgba32Float;
            }
            return image.colourSpace == ColourSpace::Srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
        }

        /** @brief Reads and writes one texel of an image format as four linear floats, for making mips. */
        struct TexelCodec
        {
            std::size_t bytes = 4;
            void (*read)(std::span<const std::byte> texel, std::array<float, 4>& out) = nullptr;
            void (*write)(const std::array<float, 4>& in, std::span<std::byte> texel) = nullptr;
        };

        /** @brief sRGB-encoded bytes to linear values, by byte. */
        const std::array<float, 256>& SrgbToLinear()
        {
            static const std::array<float, 256> table = [] {
                std::array<float, 256> made{};
                for (std::size_t i = 0; i < made.size(); ++i)
                {
                    const float c = static_cast<float>(i) / 255.0f;
                    made.at(i) = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                }
                return made;
            }();
            return table;
        }

        /** @brief Linear values, in 4096 steps over [0, 1], to sRGB-encoded bytes. */
        const std::array<std::uint8_t, 4096>& LinearToSrgb()
        {
            static const std::array<std::uint8_t, 4096> table = [] {
                std::array<std::uint8_t, 4096> made{};
                for (std::size_t i = 0; i < made.size(); ++i)
                {
                    const float l = static_cast<float>(i) / 4095.0f;
                    const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
                    made.at(i) = static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
                }
                return made;
            }();
            return table;
        }

        std::uint8_t ToByte(float value)
        {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        /** @brief The codec of an image's pixels: colours are averaged as linear values, so sRGB mips keep brightness.
         */
        TexelCodec CodecOf(const ImageData& image)
        {
            TexelCodec codec;
            codec.bytes = BytesPerPixel(image.format);
            if (image.format == ImageFormat::RGBA32F)
            {
                codec.read = [](std::span<const std::byte> texel, std::array<float, 4>& out) {
                    std::memcpy(out.data(), texel.data(), sizeof(float) * 4);
                };
                codec.write = [](const std::array<float, 4>& in, std::span<std::byte> texel) {
                    std::memcpy(texel.data(), in.data(), sizeof(float) * 4);
                };
            }
            else if (image.format == ImageFormat::RGBA16F)
            {
                codec.read = [](std::span<const std::byte> texel, std::array<float, 4>& out) {
                    std::array<std::uint16_t, 4> halves{};
                    std::memcpy(halves.data(), texel.data(), sizeof(halves));
                    for (std::size_t c = 0; c < 4; ++c)
                    {
                        out.at(c) = glm::unpackHalf1x16(halves.at(c));
                    }
                };
                codec.write = [](const std::array<float, 4>& in, std::span<std::byte> texel) {
                    std::array<std::uint16_t, 4> halves{};
                    for (std::size_t c = 0; c < 4; ++c)
                    {
                        halves.at(c) = glm::packHalf1x16(in.at(c));
                    }
                    std::memcpy(texel.data(), halves.data(), sizeof(halves));
                };
            }
            else if (image.colourSpace == ColourSpace::Srgb)
            {
                codec.read = [](std::span<const std::byte> texel, std::array<float, 4>& out) {
                    const std::span<const std::byte> bytes = texel;
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        out.at(c) = SrgbToLinear().at(static_cast<std::size_t>(bytes[c]));
                    }
                    out[3] = static_cast<float>(bytes[3]) / 255.0f;
                };
                codec.write = [](const std::array<float, 4>& in, std::span<std::byte> texel) {
                    const std::span<std::byte> bytes = texel;
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        const auto step =
                            static_cast<std::size_t>(std::lround(std::clamp(in.at(c), 0.0f, 1.0f) * 4095.0f));
                        bytes[c] = std::byte{LinearToSrgb().at(step)};
                    }
                    bytes[3] = std::byte{ToByte(in[3])};
                };
            }
            else
            {
                codec.read = [](std::span<const std::byte> texel, std::array<float, 4>& out) {
                    const std::span<const std::byte> bytes = texel;
                    for (std::size_t c = 0; c < 4; ++c)
                    {
                        out.at(c) = static_cast<float>(bytes[c]) / 255.0f;
                    }
                };
                codec.write = [](const std::array<float, 4>& in, std::span<std::byte> texel) {
                    const std::span<std::byte> bytes = texel;
                    for (std::size_t c = 0; c < 4; ++c)
                    {
                        bytes[c] = std::byte{ToByte(in.at(c))};
                    }
                };
            }
            return codec;
        }

        /**
         * @brief The next mip level of a `width` x `height` level: each texel the average of the 2 x 2 texels above it
         * (an odd last row or column is left out, and a side of 1 stays 1).
         */
        std::vector<std::byte> Downsample(std::span<const std::byte> level, std::uint32_t width, std::uint32_t height,
                                          const TexelCodec& codec)
        {
            const std::uint32_t nextWidth = std::max(1u, width / 2);
            const std::uint32_t nextHeight = std::max(1u, height / 2);
            std::vector<std::byte> next(std::size_t{nextWidth} * nextHeight * codec.bytes);
            std::array<float, 4> texel{};
            for (std::uint32_t y = 0; y < nextHeight; ++y)
            {
                for (std::uint32_t x = 0; x < nextWidth; ++x)
                {
                    std::array<float, 4> sum{};
                    for (const std::uint32_t sy : {std::min(2 * y, height - 1), std::min(2 * y + 1, height - 1)})
                    {
                        for (const std::uint32_t sx : {std::min(2 * x, width - 1), std::min(2 * x + 1, width - 1)})
                        {
                            codec.read(level.subspan((std::size_t{sy} * width + sx) * codec.bytes, codec.bytes), texel);
                            for (std::size_t c = 0; c < 4; ++c)
                            {
                                sum.at(c) += texel.at(c);
                            }
                        }
                    }
                    for (float& channel : sum)
                    {
                        channel *= 0.25f;
                    }
                    codec.write(sum,
                                std::span(next).subspan((std::size_t{y} * nextWidth + x) * codec.bytes, codec.bytes));
                }
            }
            return next;
        }

        /**
         * @brief Writes one layer of `texture` (a cube face, else 0) from `pixels`, `width` x `height`, and every mip
         * level below it, each made from the one above (ADR-0024: mips are made at upload).
         */
        void WriteWithMips(GraphicsDevice& device, TextureHandle texture, std::uint32_t layer, std::uint32_t mipLevels,
                           std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height,
                           const TexelCodec& codec)
        {
            std::vector<std::byte> level;
            std::span<const std::byte> current = pixels;
            for (std::uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                if (mip > 0)
                {
                    level = Downsample(current, width, height, codec);
                    current = level;
                    width = std::max(1u, width / 2);
                    height = std::max(1u, height / 2);
                }
                device.WriteTexture(texture, mip, layer, current);
            }
        }

        template <typename T>
        std::span<const std::byte> BytesOf(const T& value)
        {
            return std::as_bytes(std::span(&value, 1));
        }

        /** @brief The standard vertex locations a variant reads, as bits. */
        std::uint32_t ReadMask(const ShaderVariant& variant)
        {
            std::uint32_t mask = 0;
            for (const ShaderVertexInput& input : variant.vertexInputs)
            {
                if (input.location < Streams.size())
                {
                    mask |= 1u << input.location;
                }
            }
            return mask;
        }

        [[maybe_unused]] std::string_view KindName(ShaderBindingKind kind)
        {
            // By ShaderBindingKind: uniform block, storage block, texture.
            constexpr std::array<std::string_view, 3> names{"uniform block", "storage block", "texture"};
            return names.at(static_cast<std::size_t>(kind));
        }

        /**
         * @brief Why a variant's sets 0, 1 and 3 disagree with what the renderer binds there, or empty. Checked in
         * debug builds only (ADR-0024).
         */
        std::string CheckFixedSets([[maybe_unused]] const ShaderVariant& variant)
        {
#ifndef NDEBUG
            for (const ShaderBinding& binding : variant.bindings)
            {
                if (binding.set == MaterialSet)
                {
                    continue;
                }
                const auto declared = std::find_if(FixedBindings.begin(), FixedBindings.end(), [&](const auto& fixed) {
                    return fixed.set == binding.set && fixed.binding == binding.binding;
                });
                if (declared == FixedBindings.end())
                {
                    return std::format("set {} binding {} ('{}') is not a binding the renderer declares", binding.set,
                                       binding.binding, binding.name);
                }
                if (declared->kind != binding.kind)
                {
                    return std::format("set {} binding {} ('{}') is a {} in the shader, but the renderer declares a {}",
                                       binding.set, binding.binding, binding.name, KindName(binding.kind),
                                       KindName(declared->kind));
                }
                if (declared->size != 0 && declared->size != binding.size)
                {
                    return std::format("set {} binding {} ('{}') is {} bytes in the shader, but the renderer declares "
                                       "{}",
                                       binding.set, binding.binding, binding.name, binding.size, declared->size);
                }
            }
#endif
            return {};
        }

        /** @brief The parameter default of the texture parameter `name`, white if the variant does not say. */
        DefaultTexture DefaultTextureOf(const ShaderVariant& variant, std::string_view name)
        {
            const auto found = std::find_if(variant.parameters.begin(), variant.parameters.end(), [&](const auto& p) {
                return p.type == ShaderParameterType::Texture && p.name == name;
            });
            return found == variant.parameters.end() ? DefaultTexture::White : found->texture;
        }

        /** @brief Writes `material`'s number overrides that the variant's block has, at their std140 offsets. */
        void WriteOverrides(const ShaderVariant& variant, const MaterialData& material, std::vector<std::byte>& block)
        {
            for (const auto& [name, value] : material.overrides)
            {
                const auto member = std::find_if(variant.materialMembers.begin(), variant.materialMembers.end(),
                                                 [&](const ShaderBlockMember& m) { return m.name == name; });
                if (member == variant.materialMembers.end())
                {
                    continue;
                }
                // An override of another type than the member is kept by the store and ignored here.
                std::visit(
                    [&](const auto& v) {
                        using V = std::decay_t<decltype(v)>;
                        std::size_t components = 0;
                        ShaderValueType type = ShaderValueType::Other;
                        if constexpr (std::is_same_v<V, float>)
                        {
                            components = 1;
                            type = ShaderValueType::Float;
                        }
                        else if constexpr (std::is_same_v<V, Math::Vec2>)
                        {
                            components = 2;
                            type = ShaderValueType::Vec2;
                        }
                        else if constexpr (std::is_same_v<V, Math::Vec3>)
                        {
                            components = 3;
                            type = ShaderValueType::Vec3;
                        }
                        else if constexpr (std::is_same_v<V, Math::Vec4>)
                        {
                            components = 4;
                            type = ShaderValueType::Vec4;
                        }
                        if (components != 0 && member->type == type &&
                            member->offset + components * sizeof(float) <= block.size())
                        {
                            std::memcpy(std::span(block).subspan(member->offset).data(), &v,
                                        components * sizeof(float));
                        }
                    },
                    value);
            }
        }

        /** @brief Writes a `Masked` material's alpha cutoff into the variant's `alphaCutoff` float member, if it has
         * one. */
        void WriteAlphaCutoff(const ShaderVariant& variant, float cutoff, std::vector<std::byte>& block)
        {
            const auto member = std::find_if(variant.materialMembers.begin(), variant.materialMembers.end(),
                                             [](const ShaderBlockMember& m) { return m.name == AlphaCutoffMember; });
            if (member != variant.materialMembers.end() && member->type == ShaderValueType::Float &&
                member->offset + sizeof(float) <= block.size())
            {
                std::memcpy(std::span(block).subspan(member->offset).data(), &cutoff, sizeof(float));
            }
        }

        ShaderContainer ReadBuiltin(BuiltinProgram program, ILogger& logger)
        {
            ShaderContainerResult read = ReadShaderContainer(GetBuiltinShader(program).bytes);
            if (!read.Ok())
            {
                logger.Error("FenrirRenderer: the built-in shader '{}' does not read: {}",
                             GetBuiltinShader(program).name, read.diagnostic);
                return {};
            }
            return std::move(*read.container);
        }

        const ShaderVariant* BaseVariant(const ShaderContainer& container)
        {
            return container.FindVariant({});
        }

        /** @brief The container's variant with `SKINNED` added to `keys`; null if it has no such key. */
        const ShaderVariant* SkinnedVariant(const ShaderContainer& container, std::span<const std::string_view> keys)
        {
            if (std::ranges::find(container.keys, SkinnedKey) == container.keys.end())
            {
                return nullptr;
            }
            std::vector<std::string_view> wanted(keys.begin(), keys.end());
            wanted.push_back(SkinnedKey);
            return container.FindVariant(wanted);
        }

        /** @brief -1 when `world` mirrors (a negative determinant of its 3x3), else 1. */
        float WorldSign(const Math::Mat4& world)
        {
            return glm::determinant(Math::Mat3(world)) < 0.0f ? -1.0f : 1.0f;
        }

        /** @brief The device sampler a material texture parameter's description asks for. */
        SamplerDesc ToSamplerDesc(const SamplerDescription& description)
        {
            // By SamplerWrap: repeat, mirrored repeat, clamp to edge.
            constexpr std::array<AddressMode, 3> wraps{AddressMode::Repeat, AddressMode::MirroredRepeat,
                                                       AddressMode::ClampToEdge};
            SamplerDesc desc;
            desc.addressU = wraps.at(static_cast<std::size_t>(description.wrapU));
            desc.addressV = wraps.at(static_cast<std::size_t>(description.wrapV));
            desc.addressW = desc.addressU;
            const bool nearest = description.filter == SamplerFilter::Nearest;
            desc.minFilter = nearest ? Filter::Nearest : Filter::Linear;
            desc.magFilter = desc.minFilter;
            desc.mipFilter = description.filter == SamplerFilter::Trilinear ? Filter::Linear : Filter::Nearest;
            desc.maxAnisotropy = static_cast<float>(std::max<std::uint32_t>(description.anisotropy, 1));
            return desc;
        }

        /** @brief Whether `from` waits, directly or through other views, on `to`, by each view's `waitsOn`. */
        bool Waits(const std::vector<std::vector<std::size_t>>& waitsOn, std::size_t from, std::size_t to)
        {
            std::vector<bool> seen(waitsOn.size(), false);
            std::vector<std::size_t> stack{from};
            while (!stack.empty())
            {
                const std::size_t view = stack.back();
                stack.pop_back();
                if (view == to)
                {
                    return true;
                }
                if (seen[view])
                {
                    continue;
                }
                seen[view] = true;
                stack.insert(stack.end(), waitsOn[view].begin(), waitsOn[view].end());
            }
            return false;
        }

        /** @brief How a log line names a view's target: an image by its asset index, or the window. */
        std::string TargetName(AssetKey target)
        {
            return target.IsEmpty() ? std::string("the window") : std::format("image {}", target.index);
        }
    } // namespace

    // ---- SceneRenderer ----

    SceneRenderer::SceneRenderer(GraphicsDevice& device, ILogger& logger, std::uint32_t samples)
        : m_device(device), m_logger(logger), m_samples(std::max<std::uint32_t>(samples, 1))
    {
        m_frameLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "Frame");
        m_viewLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "View");
        m_drawLayout = m_device.CreateBindingGroupLayout(
            {{{0, ShaderBindingKind::StorageBuffer}, {1, ShaderBindingKind::StorageBuffer}}}, "Draw");

        // The default textures (ADR-0024): 1x1 white, black and the flat tangent-space normal, linear.
        constexpr std::array<std::array<std::uint8_t, 4>, 3> texels{{
            {255, 255, 255, 255},
            {0, 0, 0, 255},
            {128, 128, 255, 255},
        }};
        constexpr std::array<std::string_view, 3> names{"Default white", "Default black", "Default normal"};
        for (std::size_t i = 0; i < texels.size(); ++i)
        {
            TextureDesc desc;
            desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
            m_defaultTextures.at(i) = m_device.CreateTexture(desc, names.at(i));
            m_device.WriteTexture(m_defaultTextures.at(i), 0, 0, std::as_bytes(std::span(texels.at(i))));
        }
        // SamplerDescription's default: repeat, trilinear, anisotropy 8.
        SamplerDesc sampler;
        sampler.maxAnisotropy = 8.0f;
        m_defaultSampler = m_device.CreateSampler(sampler, "Default sampler");
        SamplerDesc clamp;
        clamp.addressU = AddressMode::ClampToEdge;
        clamp.addressV = AddressMode::ClampToEdge;
        clamp.addressW = AddressMode::ClampToEdge;
        m_outputSampler = m_device.CreateSampler(clamp, "Output sampler");
        // Clamped, so a face's edge does not wrap round to its opposite edge, and trilinear across the mips.
        m_skySampler = m_device.CreateSampler(clamp, "Sky sampler");
        m_defaultStreams = m_device.CreateBuffer({sizeof(DefaultStreams), BufferUsage::Vertex},
                                                 std::as_bytes(std::span(DefaultStreams)), "Default vertex streams");

        m_fallback.container = ReadBuiltin(BuiltinProgram::Fallback, m_logger);
        m_fallback.variant = BaseVariant(m_fallback.container);
        if (m_fallback.variant != nullptr)
        {
            m_fallback.materialLayout = MaterialLayoutOf(*m_fallback.variant);
            m_fallback.variants.push_back({m_fallback.variant, m_fallback.materialLayout});
            // Its SKINNED variant, which skinned draws fall back to: never drawn unskinned (ADR-0035).
            if (const ShaderVariant* skinned = SkinnedVariant(m_fallback.container, {}))
            {
                m_fallback.variants.push_back({skinned, m_fallback.materialLayout});
            }
        }
        m_fallback.id = 0;

        m_background = ReadBuiltin(BuiltinProgram::Background, m_logger);
        m_output = ReadBuiltin(BuiltinProgram::Output, m_logger);
        if (const ShaderVariant* variant = BaseVariant(m_background))
        {
            m_backgroundLayout = MaterialLayoutOf(*variant);
        }
        if (const ShaderVariant* variant = BaseVariant(m_output))
        {
            m_outputLayout = MaterialLayoutOf(*variant);
        }
        m_backgroundPipeline = MakeInternalPipeline(m_background, BaseVariant(m_background), m_backgroundLayout,
                                                    TextureFormat::Rgba16Float, true, m_samples);
        constexpr std::array<std::string_view, 1> cubemap{"CUBEMAP"};
        if (const ShaderVariant* variant = m_background.FindVariant(cubemap))
        {
            m_skyLayout = MaterialLayoutOf(*variant);
            m_skyPipeline = MakeInternalPipeline(m_background, variant, m_skyLayout, TextureFormat::Rgba16Float, true,
                                                 m_samples, m_viewLayout);
        }
        // With MSAA, Output reads every sample of SceneColor and averages them: the resolve.
        constexpr std::array<std::string_view, 1> msaa{"MSAA"};
        const ShaderVariant* output = m_samples > 1 ? m_output.FindVariant(msaa) : BaseVariant(m_output);
        m_outputPipeline = MakeInternalPipeline(m_output, output, m_outputLayout, TextureFormat::Rgba8Srgb, false, 1);

        m_frameBlocks = std::make_unique<BlockRing>(m_device, m_frameLayout, false, sizeof(FrameBlock), "Frame");
        m_viewBlocks = std::make_unique<BlockRing>(m_device, m_viewLayout, false, sizeof(ViewBlock), "View");
        m_backgroundBlocks =
            std::make_unique<BlockRing>(m_device, m_backgroundLayout, false, sizeof(BackgroundBlock), "Background");
        // The draw set's groups bind both of these rings, so the rings make none of their own.
        m_drawBlocks =
            std::make_unique<BlockRing>(m_device, BindingGroupLayoutHandle{}, true, sizeof(DrawBlock), "Draw");
        m_paletteBlocks =
            std::make_unique<BlockRing>(m_device, BindingGroupLayoutHandle{}, true, sizeof(GpuBone), "Palette");
        m_frameBlocks->Reserve(1);
    }

    SceneRenderer::~SceneRenderer()
    {
        for (auto& [key, material] : m_materials)
        {
            DestroyMaterial(material);
        }
        for (auto& [key, mesh] : m_meshes)
        {
            DestroyMesh(mesh);
        }
        for (auto& [key, target] : m_targets)
        {
            m_device.Destroy(target.texture);
        }
        for (auto& [key, image] : m_images)
        {
            m_device.Destroy(image.texture);
        }
        for (auto& [key, cubemap] : m_cubemaps)
        {
            m_device.Destroy(cubemap.group);
            m_device.Destroy(cubemap.texture);
        }
        for (auto& [desc, sampler] : m_samplers)
        {
            m_device.Destroy(sampler);
        }
        for (auto& [key, pipeline] : m_pipelines)
        {
            m_device.Destroy(pipeline.pipeline);
        }
        for (auto& [key, shader] : m_shaders)
        {
            for (const VariantEntry& variant : shader.variants)
            {
                m_device.Destroy(variant.materialLayout);
            }
        }
        for (auto& [texture, group] : m_outputGroups)
        {
            m_device.Destroy(group);
        }
        for (const DrawGroup& group : m_drawGroups)
        {
            m_device.Destroy(group.group);
        }
        m_frameBlocks.reset();
        m_viewBlocks.reset();
        m_backgroundBlocks.reset();
        m_drawBlocks.reset();
        m_paletteBlocks.reset();
        m_device.Destroy(m_fallback.materialLayout);
        m_device.Destroy(m_backgroundPipeline);
        m_device.Destroy(m_skyPipeline);
        m_device.Destroy(m_outputPipeline);
        m_device.Destroy(m_backgroundLayout);
        m_device.Destroy(m_skyLayout);
        m_device.Destroy(m_outputLayout);
        m_device.Destroy(m_frameLayout);
        m_device.Destroy(m_viewLayout);
        m_device.Destroy(m_drawLayout);
        for (const TextureHandle texture : m_defaultTextures)
        {
            m_device.Destroy(texture);
        }
        m_device.Destroy(m_defaultSampler);
        m_device.Destroy(m_outputSampler);
        m_device.Destroy(m_skySampler);
        m_device.Destroy(m_defaultStreams);
    }

    std::uint32_t SceneRenderer::NextId()
    {
        return m_nextId++;
    }

    BindingGroupLayoutHandle SceneRenderer::MaterialLayoutOf(const ShaderVariant& variant)
    {
        BindingGroupLayoutDesc desc;
        for (const ShaderBinding& binding : variant.bindings)
        {
            if (binding.set == MaterialSet)
            {
                desc.entries.push_back({binding.binding, binding.kind});
            }
        }
        if (desc.entries.empty())
        {
            return {};
        }
        return m_device.CreateBindingGroupLayout(desc, "Material");
    }

    PipelineHandle SceneRenderer::MakeInternalPipeline(const ShaderContainer& container, const ShaderVariant* variant,
                                                       BindingGroupLayoutHandle layout, TextureFormat colour,
                                                       bool depthTested, std::uint32_t samples,
                                                       BindingGroupLayoutHandle viewLayout)
    {
        PipelineDesc desc;
        desc.shader = variant;
        desc.samples = samples;
        desc.sets.at(1) = viewLayout;
        desc.sets.at(MaterialSet) = layout;
        desc.cull = CullMode::None;
        desc.colourFormats = {colour};
        desc.depth.test = depthTested;
        desc.depth.write = false;
        if (!depthTested)
        {
            desc.depthFormat.reset();
        }
        const PipelineResult made = m_device.CreatePipeline(desc, container.name);
        if (made.pipeline.IsEmpty())
        {
            m_logger.Error("FenrirRenderer: the built-in pipeline '{}' could not be made: {}", container.name,
                           made.diagnostic);
        }
        return made.pipeline;
    }

    SamplerHandle SceneRenderer::SamplerFor(const SamplerDescription& description)
    {
        const auto found = std::find_if(m_samplers.begin(), m_samplers.end(),
                                        [&](const auto& made) { return made.first == description; });
        if (found != m_samplers.end())
        {
            return found->second;
        }
        const SamplerHandle sampler = m_device.CreateSampler(ToSamplerDesc(description), "Material sampler");
        m_samplers.emplace_back(description, sampler);
        return sampler;
    }

    // ---- Prepare ----

    bool SceneRenderer::IsImageCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_images.find(key);
        return found != m_images.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareImage(AssetKey key, std::uint32_t revision, const ImageData& image)
    {
        ReleaseImage(key);
        ImageEntry& entry = m_images[key];
        entry.revision = revision;
        TextureDesc desc;
        desc.format = TextureFormatOf(image);
        desc.width = image.width;
        desc.height = image.height;
        desc.mipLevels = MipCount(image.width, image.height);
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
        entry.texture = m_device.CreateTexture(desc, "Material image");

        WriteWithMips(m_device, entry.texture, 0, desc.mipLevels, image.pixels, image.width, image.height,
                      CodecOf(image));
        ++m_stats.uploads;
    }

    void SceneRenderer::ReleaseImage(AssetKey key)
    {
        const auto found = m_images.find(key);
        if (found != m_images.end())
        {
            m_device.Destroy(found->second.texture);
            m_images.erase(found);
        }
    }

    std::vector<AssetKey> SceneRenderer::ImageKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_images)
        {
            keys.push_back(key);
        }
        return keys;
    }

    bool SceneRenderer::IsCubemapCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_cubemaps.find(key);
        return found != m_cubemaps.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareCubemap(AssetKey key, std::uint32_t revision, const ImageData& image)
    {
        ReleaseCubemap(key);
        CubemapEntry& entry = m_cubemaps[key];
        entry.revision = revision;
        TextureDesc desc;
        desc.kind = TextureKind::Cube;
        desc.format = TextureFormatOf(image);
        desc.width = image.width;
        desc.height = image.height;
        desc.mipLevels = MipCount(image.width, image.height);
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
        entry.texture = m_device.CreateTexture(desc, "Sky cubemap");
        const TexelCodec codec = CodecOf(image);
        const std::size_t faceBytes = std::size_t{image.width} * image.height * codec.bytes;
        for (std::uint32_t face = 0; face < 6; ++face)
        {
            WriteWithMips(m_device, entry.texture, face, desc.mipLevels,
                          std::span(image.pixels).subspan(face * faceBytes, faceBytes), image.width, image.height,
                          codec);
        }
        BindingGroupEntry binding;
        binding.binding = 0;
        binding.texture = entry.texture;
        binding.sampler = m_skySampler;
        entry.group = m_device.CreateBindingGroup({m_skyLayout, {binding}}, "Sky");
        ++m_stats.uploads;
    }

    void SceneRenderer::ReleaseCubemap(AssetKey key)
    {
        const auto found = m_cubemaps.find(key);
        if (found != m_cubemaps.end())
        {
            m_device.Destroy(found->second.group);
            m_device.Destroy(found->second.texture);
            m_cubemaps.erase(found);
        }
    }

    std::vector<AssetKey> SceneRenderer::CubemapKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_cubemaps)
        {
            keys.push_back(key);
        }
        return keys;
    }

    TextureHandle SceneRenderer::SampledTexture(AssetKey key) const
    {
        if (const auto target = m_targets.find(key); target != m_targets.end())
        {
            return target->second.texture;
        }
        const auto image = m_images.find(key);
        return image == m_images.end() ? TextureHandle{} : image->second.texture;
    }

    TextureHandle SceneRenderer::GetDefaultTexture(DefaultTexture texture) const
    {
        return m_defaultTextures.at(static_cast<std::size_t>(texture));
    }

    bool SceneRenderer::IsTargetCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_targets.find(key);
        return found != m_targets.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareTarget(AssetKey key, std::uint32_t revision, PixelSize size)
    {
        ReleaseTarget(key);
        TargetEntry& entry = m_targets[key];
        entry.revision = revision;
        entry.size = size;
        TextureDesc desc;
        desc.format = TextureFormat::Rgba8Srgb;
        desc.width = size.width;
        desc.height = size.height;
        desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
        entry.texture = m_device.CreateTexture(desc, "Render target image");
    }

    void SceneRenderer::ReleaseTarget(AssetKey key)
    {
        const auto found = m_targets.find(key);
        if (found != m_targets.end())
        {
            m_device.Destroy(found->second.texture);
            m_targets.erase(found);
        }
    }

    std::vector<AssetKey> SceneRenderer::TargetKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_targets)
        {
            keys.push_back(key);
        }
        return keys;
    }

    bool SceneRenderer::IsShaderCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_shaders.find(key);
        return found != m_shaders.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareShader(AssetKey key, std::uint32_t revision, const ShaderData& shader)
    {
        ReleaseShader(key);
        ShaderContainerResult read = ReadShaderContainer(shader.bytes);
        if (!read.Ok() || read.container->materialSet != MaterialSet)
        {
            m_logger.Error("FenrirRenderer: shader {} cannot draw meshes: {}", key.index,
                           read.Ok() ? "it is a 2D shader" : read.diagnostic);
        }
        // Kept even when unusable, so it is not read again until its revision changes; its materials draw as the
        // fallback.
        ShaderEntry& entry = m_shaders[key];
        entry.revision = revision;
        entry.id = NextId();
        if (read.Ok() && read.container->materialSet == MaterialSet)
        {
            entry.container = std::move(*read.container);
            entry.variant = BaseVariant(entry.container);
            for (const ShaderVariant& variant : entry.container.variants)
            {
                entry.variants.push_back({&variant, MaterialLayoutOf(variant)});
                if (&variant == entry.variant)
                {
                    entry.materialLayout = entry.variants.back().materialLayout;
                }
            }
        }
    }

    void SceneRenderer::ReleaseShader(AssetKey key)
    {
        const auto found = m_shaders.find(key);
        if (found == m_shaders.end())
        {
            return;
        }
        const ShaderEntry& shader = found->second;
        // Its materials' groups use its layout: they are made again once the shader is.
        for (auto it = m_materials.begin(); it != m_materials.end();)
        {
            if (it->second.shader == &shader)
            {
                DestroyMaterial(it->second);
                it = m_materials.erase(it);
            }
            else
            {
                ++it;
            }
        }
        DestroyPipelinesOf(shader);
        for (const VariantEntry& variant : shader.variants)
        {
            m_device.Destroy(variant.materialLayout);
        }
        m_shaders.erase(found);
    }

    void SceneRenderer::DestroyPipelinesOf(const ShaderEntry& shader)
    {
        for (auto it = m_pipelines.begin(); it != m_pipelines.end();)
        {
            if (it->first.shader == &shader)
            {
                m_device.Destroy(it->second.pipeline);
                it = m_pipelines.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    bool SceneRenderer::IsMeshCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_meshes.find(key);
        return found != m_meshes.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareMesh(AssetKey key, std::uint32_t revision, const MeshData& mesh)
    {
        ReleaseMesh(key);
        MeshEntry& entry = m_meshes[key];
        entry.revision = revision;
        entry.id = NextId();

        // Each stream the mesh has, back to back in one buffer.
        std::vector<std::byte> vertices;
        const auto append = [&](std::uint32_t location, const auto& stream) {
            if (stream.empty())
            {
                return;
            }
            entry.streams.at(location) = vertices.size();
            const std::span<const std::byte> bytes = std::as_bytes(std::span(stream));
            vertices.insert(vertices.end(), bytes.begin(), bytes.end());
        };
        append(0, mesh.positions);
        append(1, mesh.normals);
        append(2, mesh.tangents);
        append(3, mesh.uv0);
        append(4, mesh.uv1);
        append(5, mesh.colours);
        if (mesh.skin.has_value())
        {
            append(6, mesh.skin->joints);
            append(7, mesh.skin->weights);
            entry.skinned = true;
            entry.boneCount = mesh.skin->boneCount;
        }
        if (!vertices.empty())
        {
            entry.vertices = m_device.CreateBuffer({vertices.size(), BufferUsage::Vertex}, vertices, "Mesh vertices");
        }
        if (!mesh.indices.empty())
        {
            const std::span<const std::byte> indices = std::as_bytes(std::span(mesh.indices));
            entry.indices = m_device.CreateBuffer({indices.size(), BufferUsage::Index}, indices, "Mesh indices");
            entry.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        }
        ++m_stats.uploads;
    }

    void SceneRenderer::DestroyMesh(MeshEntry& mesh)
    {
        m_device.Destroy(mesh.vertices);
        m_device.Destroy(mesh.indices);
    }

    void SceneRenderer::ReleaseMesh(AssetKey key)
    {
        const auto found = m_meshes.find(key);
        if (found != m_meshes.end())
        {
            DestroyMesh(found->second);
            m_meshes.erase(found);
        }
    }

    bool SceneRenderer::IsMaterialCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_materials.find(key);
        if (found == m_materials.end() || found->second.revision != revision)
        {
            return false;
        }
        const MaterialEntry& material = found->second;
        const auto shader = m_shaders.find(material.shaderKey);
        const ShaderEntry* now = shader == m_shaders.end() ? nullptr : &shader->second;
        if (now != material.shader || (now != nullptr && now->revision != material.shaderRevision))
        {
            return false;
        }
        // An image or render target it samples was made, remade (a new revision or size) or released since.
        return std::all_of(material.images.begin(), material.images.end(),
                           [this](const SampledImage& image) { return SampledTexture(image.image) == image.bound; });
    }

    void SceneRenderer::PrepareMaterial(AssetKey key, std::uint32_t revision, const MaterialData& material)
    {
        ReleaseMaterial(key);
        MaterialEntry& entry = m_materials[key];
        entry.revision = revision;
        entry.id = NextId();
        entry.doubleSided = material.doubleSided;
        entry.blendMode = material.blendMode;
        entry.shaderKey = KeyOf(material.shader);
        const auto shader = m_shaders.find(entry.shaderKey);
        entry.shader = shader == m_shaders.end() ? nullptr : &shader->second;
        entry.shaderRevision = entry.shader == nullptr ? 0 : entry.shader->revision;
        if (entry.shader == nullptr || entry.shader->variant == nullptr)
        {
            // Drawn as the fallback material, which has no material group.
            return;
        }
        // The renderer picks the keys (ADR-0024): ALPHA_MASK for a Masked material, if the shader has it; a user
        // shader's own keys stay off.
        const bool masked = material.blendMode == Fenrir::BlendMode::Masked;
        const std::array<std::string_view, 1> maskKeys{AlphaMaskKey};
        const ShaderVariant* picked = entry.shader->container.FindVariant(
            masked ? std::span<const std::string_view>(maskKeys) : std::span<const std::string_view>());
        const auto variantEntry =
            std::find_if(entry.shader->variants.begin(), entry.shader->variants.end(), [&](const VariantEntry& v) {
                return v.variant == (picked != nullptr ? picked : entry.shader->variant);
            });
        entry.variant = &*variantEntry;
        const ShaderVariant* skinned =
            SkinnedVariant(entry.shader->container,
                           masked ? std::span<const std::string_view>(maskKeys) : std::span<const std::string_view>());
        for (const VariantEntry& v : entry.shader->variants)
        {
            if (skinned != nullptr && v.variant == skinned)
            {
                entry.skinnedVariant = &v;
            }
        }
        const ShaderVariant& variant = *entry.variant->variant;

        std::vector<BindingGroupEntry> bindings;
        for (const ShaderBinding& binding : variant.bindings)
        {
            if (binding.set != MaterialSet)
            {
                continue;
            }
            BindingGroupEntry bound;
            bound.binding = binding.binding;
            if (binding.kind == ShaderBindingKind::Texture)
            {
                // A texture left unset, or naming an image that is not prepared, binds its parameter's default texture
                // (ADR-0024) instead of adding a key.
                const TextureHandle fallback =
                    m_defaultTextures.at(static_cast<std::size_t>(DefaultTextureOf(variant, binding.name)));
                bound.texture = fallback;
                bound.sampler = m_defaultSampler;
                const auto value = material.overrides.find(binding.name);
                const MaterialTexture* texture =
                    value == material.overrides.end() ? nullptr : std::get_if<MaterialTexture>(&value->second);
                if (texture != nullptr)
                {
                    bound.sampler = SamplerFor(texture->sampler);
                }
                if (texture != nullptr && !texture->image.IsEmpty())
                {
                    // A render target image binds its texture (ADR-0026); an image with pixels its uploaded texture.
                    SampledImage image{bindings.size(), KeyOf(texture->image), SampledTexture(KeyOf(texture->image)),
                                       fallback};
                    if (!image.bound.IsEmpty())
                    {
                        bound.texture = image.bound;
                    }
                    entry.images.push_back(image);
                }
            }
            else if (variant.materialBlockBinding == binding.binding && !variant.defaultBlock.empty())
            {
                // The default block image, then the material's overrides over it (ADR-0024), and a Masked material's
                // alpha cutoff over those.
                std::vector<std::byte> block = variant.defaultBlock;
                WriteOverrides(variant, material, block);
                if (masked)
                {
                    WriteAlphaCutoff(variant, material.alphaCutoff, block);
                }
                entry.block = m_device.CreateBuffer({block.size(), BufferUsage::Uniform}, block, "Material block");
                bound.buffer = entry.block;
                ++m_stats.uploads;
            }
            bindings.push_back(bound);
        }
        entry.layout = entry.variant->materialLayout;
        entry.bindings = bindings;
        if (!entry.layout.IsEmpty())
        {
            entry.group = m_device.CreateBindingGroup({entry.layout, std::move(bindings)}, "Material");
        }
    }

    BindingGroupHandle SceneRenderer::GroupWithout(MaterialEntry& material, const std::vector<AssetKey>& replaced)
    {
        std::vector<AssetKey> mine;
        for (const SampledImage& image : material.images)
        {
            if (!image.bound.IsEmpty() && std::find(replaced.begin(), replaced.end(), image.image) != replaced.end())
            {
                mine.push_back(image.image);
            }
        }
        if (mine.empty())
        {
            return material.group;
        }
        std::sort(mine.begin(), mine.end());
        mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
        if (const auto found = material.withoutTargets.find(mine); found != material.withoutTargets.end())
        {
            return found->second;
        }
        std::vector<BindingGroupEntry> bindings = material.bindings;
        for (const SampledImage& image : material.images)
        {
            if (std::binary_search(mine.begin(), mine.end(), image.image))
            {
                bindings.at(image.binding).texture = image.fallback;
                bindings.at(image.binding).sampler = m_defaultSampler;
            }
        }
        const BindingGroupHandle group =
            m_device.CreateBindingGroup({material.layout, std::move(bindings)}, "Material");
        material.withoutTargets.emplace(std::move(mine), group);
        return group;
    }

    void SceneRenderer::DestroyMaterial(MaterialEntry& material)
    {
        for (const auto& [replaced, group] : material.withoutTargets)
        {
            m_device.Destroy(group);
        }
        m_device.Destroy(material.group);
        m_device.Destroy(material.block);
    }

    void SceneRenderer::ReleaseMaterial(AssetKey key)
    {
        const auto found = m_materials.find(key);
        if (found != m_materials.end())
        {
            DestroyMaterial(found->second);
            m_materials.erase(found);
        }
    }

    std::vector<AssetKey> SceneRenderer::ShaderKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_shaders)
        {
            keys.push_back(key);
        }
        return keys;
    }

    std::vector<AssetKey> SceneRenderer::MeshKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_meshes)
        {
            keys.push_back(key);
        }
        return keys;
    }

    std::vector<AssetKey> SceneRenderer::MaterialKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_materials)
        {
            keys.push_back(key);
        }
        return keys;
    }

    // ---- The frame ----

    void SceneRenderer::SetFrame(SceneFrame frame)
    {
        m_frame = std::move(frame);
    }

    SceneStats SceneRenderer::TakeStats()
    {
        const SceneStats stats = m_stats;
        m_stats.uploads = 0;
        return stats;
    }

    const SceneRenderer::PipelineEntry& SceneRenderer::MeshPipeline(const ShaderEntry& shader,
                                                                    const VariantEntry& variant, const MeshEntry& mesh,
                                                                    Fenrir::BlendMode blend, bool doubleSided)
    {
        const std::uint32_t read = variant.variant != nullptr ? ReadMask(*variant.variant) : 0;
        std::uint32_t present = 0;
        for (std::uint32_t location = 0; location < Streams.size(); ++location)
        {
            if (mesh.streams.at(location).has_value())
            {
                present |= 1u << location;
            }
        }
        const PipelineKey key{&shader, variant.variant, read & present, blend, doubleSided, m_samples};
        if (const auto found = m_pipelines.find(key); found != m_pipelines.end())
        {
            return found->second;
        }

        PipelineEntry& entry = m_pipelines[key];
        entry.id = NextId();
        if (variant.variant == nullptr)
        {
            entry.failure = "it has no variant to draw";
            return entry;
        }
        entry.failure = CheckFixedSets(*variant.variant);
        PipelineDesc desc;
        desc.shader = variant.variant;
        desc.sets = {m_frameLayout, m_viewLayout, variant.materialLayout, m_drawLayout};
        for (const ShaderVertexInput& input : variant.variant->vertexInputs)
        {
            if (input.location >= Streams.size())
            {
                entry.failure = std::format("it reads vertex input '{}' at location {}, which no mesh stream feeds",
                                            input.name, input.location);
                break;
            }
            const StreamFormat& stream = Streams.at(input.location);
            // A stream the mesh lacks reads one constant: a stride of 0 (ADR-0024).
            const bool has = (key.streams & (1u << input.location)) != 0;
            desc.vertexBuffers.push_back(
                {has ? static_cast<std::uint32_t>(stream.size) : 0u, {{input.location, stream.format, 0}}});
            entry.locations.push_back(input.location);
        }
        // The render state (ADR-0024): Opaque and Masked write depth; AlphaBlend and Additive blend without writing it.
        // Depth is always tested, greater-or-equal for reversed-Z, and doubleSided turns culling off.
        desc.cull = doubleSided ? CullMode::None : CullMode::Back;
        desc.depth.write = !IsTransparent(blend);
        desc.blend = DeviceBlendOf(blend);
        desc.colourFormats = {TextureFormat::Rgba16Float};
        desc.samples = m_samples;
        if (entry.failure.empty())
        {
            PipelineResult made = m_device.CreatePipeline(desc, shader.container.name);
            entry.pipeline = made.pipeline;
            entry.failure = std::move(made.diagnostic);
        }
        if (!entry.failure.empty())
        {
            m_logger.Error("FenrirRenderer: no pipeline for shader '{}', so its materials draw as the fallback "
                           "material: {}",
                           shader.container.name, entry.failure);
        }
        return entry;
    }

    std::vector<SceneRenderer::ResolvedDraw> SceneRenderer::ResolveDraws()
    {
        std::vector<ResolvedDraw> draws;
        draws.reserve(m_frame.draws.size());
        for (const DrawRequest& request : m_frame.draws)
        {
            const auto mesh = m_meshes.find(request.mesh);
            if (mesh == m_meshes.end() || mesh->second.indexCount == 0)
            {
                continue;
            }
            // Only a mesh with skin data draws skinned; the facade asks for it only then.
            const bool skinned = request.skinned && mesh->second.skinned;
            if (skinned && std::uint64_t{request.paletteOffset} + mesh->second.boneCount > m_frame.palette.size())
            {
                m_logger.Error("FenrirRenderer: internal error: a skinned draw of mesh {} reads palette entries {} to "
                               "{}, past the {} written this frame, so it is not drawn",
                               request.mesh.index, request.paletteOffset,
                               std::uint64_t{request.paletteOffset} + mesh->second.boneCount, m_frame.palette.size());
                continue;
            }
            const auto material = m_materials.find(request.material);
            MaterialEntry* found = material == m_materials.end() ? nullptr : &material->second;
            bool usable = found != nullptr && found->variant != nullptr;
            if (skinned && usable && found->skinnedVariant == nullptr)
            {
                // Never drawn unskinned (ADR-0035): the fallback material's SKINNED variant draws it instead.
                if (m_loggedUnskinned.insert(found->shaderKey).second)
                {
                    m_logger.Warn("FenrirRenderer: shader '{}' has no SKINNED variant, so its skinned meshes are drawn "
                                  "with the fallback material",
                                  found->shader->container.name);
                }
                usable = false;
            }
            const VariantEntry& fallbackVariant =
                skinned && m_fallback.variants.size() > 1 ? m_fallback.variants[1] : m_fallback.variants.front();
            const ShaderEntry* shader = usable ? found->shader : &m_fallback;
            const VariantEntry* variant =
                usable ? (skinned ? found->skinnedVariant : found->variant) : &fallbackVariant;
            Fenrir::BlendMode blend = usable ? found->blendMode : Fenrir::BlendMode::Opaque;
            const PipelineEntry* pipeline =
                &MeshPipeline(*shader, *variant, mesh->second, blend, usable && found->doubleSided);
            if (pipeline->pipeline.IsEmpty() && shader != &m_fallback)
            {
                shader = &m_fallback;
                variant = &fallbackVariant;
                blend = Fenrir::BlendMode::Opaque;
                pipeline = &MeshPipeline(m_fallback, *variant, mesh->second, blend, false);
            }
            if (pipeline->pipeline.IsEmpty())
            {
                continue;
            }

            for (const ShaderVertexInput& input : variant->variant->vertexInputs)
            {
                if (input.location < Streams.size() && !mesh->second.streams.at(input.location).has_value() &&
                    m_loggedStreams.insert({request.mesh, input.location}).second)
                {
                    const StreamFormat& stream = Streams.at(input.location);
                    m_logger.Warn("FenrirRenderer: mesh {} has no {}, which shader '{}' reads; it is drawn with {}",
                                  request.mesh.index, stream.name, shader->container.name, stream.defaultName);
                }
            }

            const bool fallback = shader == &m_fallback;
            ResolvedDraw draw;
            draw.pipeline = pipeline;
            draw.material = fallback ? nullptr : found;
            draw.mesh = &mesh->second;
            draw.world = request.world;
            draw.transparent = IsTransparent(blend);
            draw.paletteOffset = skinned ? request.paletteOffset : 0;
            draw.sortPoint = request.sortPoint.value_or(Math::Vec3(request.world[3]));
            const std::uint64_t stage = draw.transparent ? TransparentStage : OpaqueStage;
            const std::uint64_t materialId = fallback ? 0 : found->id;
            draw.sortKey = (stage << 56u) | ((std::uint64_t{pipeline->id} & 0xFFFFu) << 40u) |
                           ((materialId & 0xFFFFFu) << 20u) | (std::uint64_t{mesh->second.id} & 0xFFFFFu);
            draws.push_back(draw);
        }
        std::stable_sort(draws.begin(), draws.end(),
                         [](const ResolvedDraw& a, const ResolvedDraw& b) { return a.sortKey < b.sortKey; });
        return draws;
    }

    void SceneRenderer::RecordDraws(PassEncoder& encoder, const std::vector<ResolvedDraw>& draws,
                                    const std::vector<BindingGroupHandle>& groups,
                                    const std::vector<std::size_t>& order, BindingGroupHandle frameGroup,
                                    BindingGroupHandle viewGroup, BindingGroupHandle drawGroup,
                                    PixelRect viewport) const
    {
        encoder.SetViewport(viewport);
        const PipelineEntry* current = nullptr;
        BindingGroupHandle material;
        for (const std::size_t i : order)
        {
            const ResolvedDraw& draw = draws[i];
            const BindingGroupHandle group = groups[i];
            const bool newPipeline = draw.pipeline != current;
            if (newPipeline)
            {
                current = draw.pipeline;
                encoder.SetPipeline(current->pipeline);
                encoder.SetBindingGroup(0, frameGroup);
                encoder.SetBindingGroup(1, viewGroup);
                encoder.SetBindingGroup(3, drawGroup);
            }
            if (!group.IsEmpty() && (newPipeline || group != material))
            {
                encoder.SetBindingGroup(MaterialSet, group);
            }
            material = group;
            for (std::size_t slot = 0; slot < current->locations.size(); ++slot)
            {
                const std::uint32_t location = current->locations[slot];
                const std::optional<std::size_t>& offset = draw.mesh->streams.at(location);
                if (offset.has_value())
                {
                    encoder.SetVertexBuffer(static_cast<std::uint32_t>(slot), draw.mesh->vertices, *offset);
                }
                else
                {
                    encoder.SetVertexBuffer(static_cast<std::uint32_t>(slot), m_defaultStreams,
                                            Streams.at(location).defaultOffset);
                }
            }
            encoder.SetIndexBuffer(draw.mesh->indices, IndexFormat::UInt32);
            // Each item reads its own record of the draw buffer, by its place in the draw list.
            encoder.DrawIndexed({.indexCount = draw.mesh->indexCount, .firstInstance = static_cast<std::uint32_t>(i)});
        }
    }

    BindingGroupHandle SceneRenderer::DrawGroupFor(std::uint64_t frameIndex)
    {
        const BufferHandle draws = m_drawBlocks->Buffer(frameIndex);
        const BufferHandle palette = m_paletteBlocks->Buffer(frameIndex);
        // One group per copy, made again when either ring has remade its buffers.
        const std::size_t copies = std::max<std::size_t>(m_device.FramesInFlight(), 1);
        m_drawGroups.resize(copies);
        DrawGroup& group = m_drawGroups[static_cast<std::size_t>(frameIndex % copies)];
        if (group.group.IsEmpty() || group.draws != draws || group.palette != palette)
        {
            m_device.Destroy(group.group);
            BindingGroupEntry drawEntry;
            drawEntry.binding = 0;
            drawEntry.buffer = draws;
            BindingGroupEntry paletteEntry;
            paletteEntry.binding = 1;
            paletteEntry.buffer = palette;
            group = {draws, palette, m_device.CreateBindingGroup({m_drawLayout, {drawEntry, paletteEntry}}, "Draw")};
        }
        return group.group;
    }

    BindingGroupHandle SceneRenderer::OutputGroup(TextureHandle sceneColour)
    {
        const std::pair<std::uint32_t, std::uint32_t> key{sceneColour.index, sceneColour.generation};
        if (const auto found = m_outputGroups.find(key); found != m_outputGroups.end())
        {
            return found->second;
        }
        BindingGroupEntry entry;
        entry.binding = 0;
        entry.texture = sceneColour;
        entry.sampler = m_outputSampler;
        const BindingGroupHandle group = m_device.CreateBindingGroup({m_outputLayout, {entry}}, "Output");
        m_outputGroups.emplace(key, group);
        return group;
    }

    void SceneRenderer::AddWindowClear(RenderGraph& graph, const FrameContext& frame)
    {
        // A window no view draws into is cleared to black.
        const GraphResource backbuffer = frame.backbuffer;
        graph.AddPass(
            "Clear", RenderStage::Output, QueueKind::Graphics,
            [=](PassBuilder& pass) { pass.ColourAttachment(backbuffer, LoadOp::Clear, {0.0f, 0.0f, 0.0f, 1.0f}); },
            [](PassContext&) {});
    }

    void SceneRenderer::AddPasses(RenderGraph& graph, const FrameContext& frame)
    {
        // The Output groups of SceneColor textures the transient pool has let go of.
        for (auto it = m_outputGroups.begin(); it != m_outputGroups.end();)
        {
            if (!m_device.IsLive(TextureHandle{it->first.first, it->first.second}))
            {
                m_device.Destroy(it->second);
                it = m_outputGroups.erase(it);
            }
            else
            {
                ++it;
            }
        }

        m_stats.draws = 0;
        m_drawnViews.clear();
        if (m_frame.views.empty())
        {
            AddWindowClear(graph, frame);
            return;
        }

        const auto draws = std::make_shared<const std::vector<ResolvedDraw>>(ResolveDraws());
        const std::uint64_t frameIndex = frame.frameIndex;
        const SceneLighting& lighting = m_frame.lighting;
        const FrameBlock frameBlock{Math::Vec4(lighting.lightDirection, 0.0f), Math::Vec4(lighting.lightColour, 0.0f),
                                    Math::Vec4(lighting.ambient, 0.0f),
                                    Math::Vec4(m_frame.time, m_frame.deltaTime, 0.0f, 0.0f)};
        m_frameBlocks->Write(frameIndex, 0, BytesOf(frameBlock));
        m_drawBlocks->Reserve(draws->size());
        for (std::size_t i = 0; i < draws->size(); ++i)
        {
            const ResolvedDraw& draw = (*draws)[i];
            DrawBlock block;
            block.world = draw.world;
            block.normal = glm::transpose(glm::inverse(draw.world));
            block.paletteOffset = draw.paletteOffset;
            block.worldSign = WorldSign(draw.world);
            m_drawBlocks->Write(frameIndex, i, BytesOf(block));
        }
        // Every palette written again each frame (ADR-0035): nothing is kept across frames.
        m_paletteBlocks->Reserve(m_frame.palette.size());
        for (std::size_t bone = 0; bone < m_frame.palette.size(); ++bone)
        {
            m_paletteBlocks->Write(frameIndex, bone, BytesOf(GpuBone::FromMatrix(m_frame.palette[bone])));
        }
        const std::vector<ViewRequest>& views = m_frame.views;
        m_viewBlocks->Reserve(views.size());
        m_backgroundBlocks->Reserve(views.size());

        // The render target images this frame's draws sample, each imported into the graph once.
        std::set<AssetKey> sampled;
        for (const ResolvedDraw& draw : *draws)
        {
            if (draw.material != nullptr)
            {
                for (const SampledImage& image : draw.material->images)
                {
                    if (!image.bound.IsEmpty() && m_targets.contains(image.image))
                    {
                        sampled.insert(image.image);
                    }
                }
            }
        }
        std::map<AssetKey, GraphResource> images;
        const auto imported = [&](AssetKey key) {
            if (const auto found = images.find(key); found != images.end())
            {
                return found->second;
            }
            const TargetEntry& target = m_targets.at(key);
            TextureDesc desc;
            desc.format = TextureFormat::Rgba8Srgb;
            desc.width = target.size.width;
            desc.height = target.size.height;
            desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
            const GraphResource resource =
                graph.ImportTexture(std::format("Image {}", key.index), target.texture, desc);
            images.emplace(key, resource);
            return resource;
        };

        // Which sampled images each view may read. A view samples an image after every view that draws it, so the
        // graph orders them; one that would wait on itself, directly or through other views, gets the default texture
        // for that image instead (ADR-0026), the views taken in their order.
        std::vector<std::vector<std::size_t>> waitsOn(views.size());
        std::vector<std::vector<AssetKey>> replaced(views.size());
        std::vector<std::vector<AssetKey>> reads(views.size());
        for (std::size_t v = 0; v < views.size(); ++v)
        {
            for (const AssetKey image : sampled)
            {
                std::vector<std::size_t> writers;
                for (std::size_t w = 0; w < views.size(); ++w)
                {
                    if (views[w].target == image && m_targets.contains(image))
                    {
                        writers.push_back(w);
                    }
                }
                const bool own = views[v].target == image;
                const bool cycle =
                    std::any_of(writers.begin(), writers.end(), [&](std::size_t w) { return Waits(waitsOn, w, v); });
                if (own || cycle)
                {
                    replaced[v].push_back(image);
                    if (m_loggedSampling.insert({views[v].target, image}).second)
                    {
                        m_logger.Warn(own ? "FenrirRenderer: a view into {} draws materials that sample its own target "
                                            "image, so they sample the default texture there"
                                          : "FenrirRenderer: a view into {} draws materials that sample image {}, "
                                            "whose views wait on it in a cycle, so they sample the default texture "
                                            "there",
                                      TargetName(views[v].target), image.index);
                    }
                    continue;
                }
                waitsOn[v].insert(waitsOn[v].end(), writers.begin(), writers.end());
                reads[v].push_back(image);
            }
        }

        const BindingGroupHandle frameGroup = m_frameBlocks->Group(frameIndex, 0);
        const BindingGroupHandle drawGroup = DrawGroupFor(frameIndex);
        // The targets drawn into so far: the first view into each clears it. The window's key is empty.
        std::set<AssetKey> cleared;
        for (std::size_t v = 0; v < views.size(); ++v)
        {
            const ViewRequest& request = views[v];
            const bool intoImage = !request.target.IsEmpty();
            if (intoImage && !m_targets.contains(request.target))
            {
                continue;
            }
            const GraphResource target = intoImage ? imported(request.target) : frame.backbuffer;
            const PixelRect rect = request.viewport;
            const PixelRect viewport{0, 0, rect.width, rect.height};

            const Math::Mat4 viewProjection = request.projection * request.view;
            const ViewBlock viewBlock{request.view,
                                      request.projection,
                                      viewProjection,
                                      glm::inverse(viewProjection),
                                      Math::Vec4(request.position, 1.0f),
                                      Math::Vec4(static_cast<float>(rect.x), static_cast<float>(rect.y),
                                                 static_cast<float>(rect.width), static_cast<float>(rect.height))};
            m_viewBlocks->Write(frameIndex, v, BytesOf(viewBlock));
            const BackgroundBlock backgroundBlock{
                Math::Vec4(request.background.r, request.background.g, request.background.b, request.background.a)};
            m_backgroundBlocks->Write(frameIndex, v, BytesOf(backgroundBlock));
            const BindingGroupHandle viewGroup = m_viewBlocks->Group(frameIndex, v);
            // The Sky cubemap's pipeline and group, when the view draws one that is prepared; else its colour's.
            BindingGroupHandle backgroundGroup = m_backgroundBlocks->Group(frameIndex, v);
            PipelineHandle backgroundPipeline = m_backgroundPipeline;
            if (const auto sky = m_cubemaps.find(request.sky); sky != m_cubemaps.end() && !m_skyPipeline.IsEmpty())
            {
                backgroundGroup = sky->second.group;
                backgroundPipeline = m_skyPipeline;
            }

            // Each draw's material group in this view, with the images it may not sample replaced.
            auto groups = std::make_shared<std::vector<BindingGroupHandle>>();
            groups->reserve(draws->size());
            for (const ResolvedDraw& draw : *draws)
            {
                groups->push_back(draw.material == nullptr ? BindingGroupHandle{}
                                                           : GroupWithout(*draw.material, replaced[v]));
            }
            std::vector<GraphResource> samples;
            for (const AssetKey image : reads[v])
            {
                samples.push_back(imported(image));
            }

            TextureDesc colourDesc;
            colourDesc.format = TextureFormat::Rgba16Float;
            colourDesc.width = rect.width;
            colourDesc.height = rect.height;
            colourDesc.samples = m_samples;
            colourDesc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
            TextureDesc depthDesc = colourDesc;
            depthDesc.format = TextureFormat::Depth32Float;
            depthDesc.usage = TextureUsage::DepthAttachment;
            const GraphResource sceneColour = graph.CreateTexture("SceneColor", colourDesc);
            const GraphResource sceneDepth = graph.CreateTexture("SceneDepth", depthDesc);
            m_drawnViews.push_back({v, sceneColour, sceneDepth, viewGroup});

            // Opaque items in draw-list order; transparent ones back to front by the view-space depth of their world
            // origins, the farthest (most negative view z) first.
            auto opaque = std::make_shared<std::vector<std::size_t>>();
            auto transparent = std::make_shared<std::vector<std::size_t>>();
            std::vector<float> depth(draws->size(), 0.0f);
            for (std::size_t i = 0; i < draws->size(); ++i)
            {
                const ResolvedDraw& draw = (*draws)[i];
                (draw.transparent ? *transparent : *opaque).push_back(i);
                depth[i] = (request.view * Math::Vec4(draw.sortPoint, 1.0f)).z;
            }
            std::stable_sort(transparent->begin(), transparent->end(),
                             [&](std::size_t a, std::size_t b) { return depth[a] < depth[b]; });

            graph.AddPass(
                "Opaque", RenderStage::Opaque, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.ColourAttachment(sceneColour, LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f});
                    pass.DepthAttachment(sceneDepth, LoadOp::Clear, 0.0f);
                    for (const GraphResource image : samples)
                    {
                        pass.Sample(image);
                    }
                },
                [this, draws, groups, opaque, frameGroup, viewGroup, drawGroup, viewport](PassContext& context) {
                    RecordDraws(context.Encoder(), *draws, *groups, *opaque, frameGroup, viewGroup, drawGroup,
                                viewport);
                });
            if (!transparent->empty())
            {
                // Depth tested against the opaque items, without writing it (ADR-0024).
                graph.AddPass(
                    "Transparent", RenderStage::Transparent, QueueKind::Graphics,
                    [=](PassBuilder& pass) {
                        pass.ColourAttachment(sceneColour, LoadOp::Load);
                        pass.DepthReadOnly(sceneDepth);
                        for (const GraphResource image : samples)
                        {
                            pass.Sample(image);
                        }
                    },
                    [this, draws, groups, transparent, frameGroup, viewGroup, drawGroup,
                     viewport](PassContext& context) {
                        RecordDraws(context.Encoder(), *draws, *groups, *transparent, frameGroup, viewGroup, drawGroup,
                                    viewport);
                    });
            }
            m_stats.draws += static_cast<std::uint32_t>(draws->size());

            graph.AddPass(
                "Background", RenderStage::Background, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.ColourAttachment(sceneColour, LoadOp::Load);
                    pass.DepthReadOnly(sceneDepth);
                },
                [backgroundPipeline, backgroundGroup, viewGroup, viewport](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(viewport);
                    encoder.SetPipeline(backgroundPipeline);
                    encoder.SetBindingGroup(1, viewGroup);
                    encoder.SetBindingGroup(MaterialSet, backgroundGroup);
                    encoder.Draw({.vertexCount = 3});
                });

            // The first view into a target clears all of it; each later one draws over it, in its viewport only.
            const LoadOp load = cleared.insert(request.target).second ? LoadOp::Clear : LoadOp::Load;
            graph.AddPass(
                "Output", RenderStage::Output, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.Sample(sceneColour);
                    pass.ColourAttachment(target, load, {0.0f, 0.0f, 0.0f, 1.0f});
                },
                [this, sceneColour, rect](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(rect);
                    encoder.SetPipeline(m_outputPipeline);
                    encoder.SetBindingGroup(MaterialSet, OutputGroup(context.Texture(sceneColour)));
                    encoder.Draw({.vertexCount = 3});
                });
        }
        if (!cleared.contains(AssetKey{}))
        {
            AddWindowClear(graph, frame);
        }
    }

    Math::Mat4 ReversedZOrthographic(float height, float aspect, float near, float far)
    {
        Math::Mat4 projection(1.0f);
        projection[0][0] = 2.0f / (height * aspect);
        projection[1][1] = 2.0f / height;
        // Depth is (far + z) / (far - near) for view-space z = -distance: 1 at the near plane, 0 at the far one.
        projection[2][2] = 1.0f / (far - near);
        projection[3][2] = far / (far - near);
        return projection;
    }

    PixelRect ViewportPixels(float x, float y, float width, float height, PixelSize size)
    {
        const auto edge = [](float fraction, std::uint32_t pixels) {
            const float at = std::round(fraction * static_cast<float>(pixels));
            return static_cast<std::int64_t>(std::clamp(at, 0.0f, static_cast<float>(pixels)));
        };
        const std::int64_t left = edge(x, size.width);
        const std::int64_t right = edge(x + width, size.width);
        const std::int64_t top = edge(y, size.height);
        const std::int64_t bottom = edge(y + height, size.height);
        if (right <= left || bottom <= top)
        {
            return {};
        }
        return {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)};
    }

    Math::Mat4 ReversedZPerspective(float verticalFov, float aspect, float near)
    {
        const float focal = 1.0f / std::tan(verticalFov * 0.5f);
        Math::Mat4 projection(0.0f);
        projection[0][0] = focal / aspect;
        projection[1][1] = focal;
        // Depth is near / -z: 1 at the near plane, falling to 0 at infinity.
        projection[3][2] = near;
        projection[2][3] = -1.0f;
        return projection;
    }
} // namespace Fenrir::Render
