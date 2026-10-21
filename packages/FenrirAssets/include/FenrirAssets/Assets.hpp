#pragma once

// SPIKE (prototype/fenrir-renderer-spike, throwaway): the least of ADR-0021's FenrirAssets the renderer needs. Plain
// CPU-side data behind generational handles, Add/Replace only (no loading, no ownership by scene, no dedup).

#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace Fenrir
{
    class App;

    template <typename T>
    struct AssetHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0; // 0: empty

        [[nodiscard]] bool IsValid() const
        {
            return generation != 0;
        }

        friend bool operator==(const AssetHandle&, const AssetHandle&) = default;
    };

    struct Vertex
    {
        Math::Vec3 position{0.0f};
        Math::Vec3 normal{0.0f, 1.0f, 0.0f};
        Math::Vec2 uv{0.0f};
        Colour colour{};
    };

    struct MeshData
    {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices; // three per triangle, counter-clockwise seen from the front
        Math::Vec3 boundsMin{0.0f};
        Math::Vec3 boundsMax{0.0f};
    };

    enum class ImageFormat
    {
        Rgba8Srgb,
        Rgba8Linear,
    };

    struct ImageData
    {
        int width = 0;
        int height = 0;
        ImageFormat format = ImageFormat::Rgba8Srgb;
        bool cube = false;                // six faces, +X -X +Y -Y +Z -Z, one after the other
        std::vector<std::uint8_t> pixels; // RGBA8
    };

    enum class BlendMode
    {
        Opaque,
        AlphaBlend,
    };

    using MaterialValue = std::variant<float, Math::Vec4, Colour, AssetHandle<ImageData>>;

    struct MaterialData
    {
        std::string shader = "builtin:lit"; // or "builtin:unlit"
        BlendMode blend = BlendMode::Opaque;
        bool doubleSided = false;
        std::map<std::string, MaterialValue> params;
    };

    /** @brief App resource: every asset, by type. Revisions start at 1 and bump on Replace. */
    class Assets
    {
      public:
        static Assets& Install(App& app);

        template <typename T>
        AssetHandle<T> Add(T data)
        {
            auto& store = Store<T>();
            store.push_back({std::move(data), 1, 1});
            return {static_cast<std::uint32_t>(store.size() - 1), 1};
        }

        template <typename T>
        bool Replace(AssetHandle<T> handle, T data)
        {
            Slot<T>* slot = Find(handle);
            if (slot == nullptr)
            {
                return false;
            }
            slot->data = std::move(data);
            ++slot->revision;
            return true;
        }

        template <typename T>
        [[nodiscard]] const T* Get(AssetHandle<T> handle) const
        {
            const Slot<T>* slot = const_cast<Assets*>(this)->Find(handle);
            return slot ? &slot->data : nullptr;
        }

        template <typename T>
        [[nodiscard]] std::uint32_t Revision(AssetHandle<T> handle) const
        {
            const Slot<T>* slot = const_cast<Assets*>(this)->Find(handle);
            return slot ? slot->revision : 0;
        }

      private:
        template <typename T>
        struct Slot
        {
            T data;
            std::uint32_t revision = 1;
            std::uint32_t generation = 1;
        };

        template <typename T>
        std::vector<Slot<T>>& Store();

        template <typename T>
        Slot<T>* Find(AssetHandle<T> handle)
        {
            auto& store = Store<T>();
            if (!handle.IsValid() || handle.index >= store.size() ||
                store[handle.index].generation != handle.generation)
            {
                return nullptr;
            }
            return &store[handle.index];
        }

        std::vector<Slot<MeshData>> m_meshes;
        std::vector<Slot<ImageData>> m_images;
        std::vector<Slot<MaterialData>> m_materials;
    };

    template <>
    inline std::vector<Assets::Slot<MeshData>>& Assets::Store<MeshData>()
    {
        return m_meshes;
    }
    template <>
    inline std::vector<Assets::Slot<ImageData>>& Assets::Store<ImageData>()
    {
        return m_images;
    }
    template <>
    inline std::vector<Assets::Slot<MaterialData>>& Assets::Store<MaterialData>()
    {
        return m_materials;
    }

    /** @brief Procedural meshes, made for Add. */
    namespace Primitives
    {
        [[nodiscard]] MeshData Cube(float halfExtent = 0.5f);
        [[nodiscard]] MeshData Sphere(float radius = 0.5f, int segments = 32, int rings = 16);
        [[nodiscard]] MeshData Plane(float halfExtent = 10.0f, float uvRepeat = 10.0f);
    } // namespace Primitives
} // namespace Fenrir
