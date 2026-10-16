#pragma once

// DemoMesh: what an entity looks like, as plain data, so code that must not know the renderer (C# scripts) can make
// visible objects. DemoMeshes is the system that turns it into the renderer's Model and Material.

#include <cstdint>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

#include "Material.hpp"
#include "ModelLibrary.hpp"
#include "ShaderLibrary.hpp"

/** @brief Which of the demo's models an entity is drawn with. Both are one unit across, centred on the entity, so the
 * entity's Transform scale is the size, matching a physics collider's default shape. */
enum class MeshKind : std::int32_t
{
    Cube = 0,
    Sphere = 1,
};

/** @brief Draws the entity as Kind, in one shaded colour. */
struct DemoMesh
{
    MeshKind Kind = MeshKind::Cube;
    Fenrir::Math::Vec3 Colour = Fenrir::Math::Vec3(1.0f, 1.0f, 1.0f); // RGB, 0 to 1.
};

/** @brief In Update, before the renderer draws: gives each entity with a DemoMesh and no Model the Model its Kind names
 * and a Material in its Colour, and keeps every such Material's colour equal to its DemoMesh's Colour, so a colour
 * written this frame shows this frame. Kind is read once, when the Model is given. */
class DemoMeshes
{
  public:
    /** @brief The models and shader are referred to, not copied: they are loaded in Init, after this is made. */
    DemoMeshes(const Model& cube, const Model& sphere, const Shader& shader)
        : m_cube(&cube), m_sphere(&sphere), m_shader(&shader)
    {
    }

    void Update(Fenrir::App& app)
    {
        Fenrir::EntityList& entities = app.GetActiveScene().GetEntityList();

        m_unshown.clear();
        entities.ForEach<DemoMesh>([this, &entities](auto handle, DemoMesh& mesh) {
            Fenrir::Entity entity = entities.GetEntity(static_cast<std::uint32_t>(handle));
            if (!entity.HasComponent<Model>() || !entity.HasComponent<Material>())
            {
                m_unshown.push_back(entity); // Given its components after the loop, which must not add any.
                return;
            }
            entity.GetComponent<Material>().properties["color"] = mesh.Colour;
        });

        for (Fenrir::Entity& entity : m_unshown)
        {
            const DemoMesh& mesh = entity.GetComponent<DemoMesh>();
            Material material{*m_shader, {}};
            material.properties["color"] = mesh.Colour;
            entity.AddComponent<Material>(material);
            entity.AddComponent<Model>(mesh.Kind == MeshKind::Sphere ? *m_sphere : *m_cube);
        }
    }

  private:
    const Model* m_cube;
    const Model* m_sphere;
    const Shader* m_shader;
    std::vector<Fenrir::Entity> m_unshown;
};
