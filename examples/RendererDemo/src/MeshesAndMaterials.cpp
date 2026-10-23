#include "RendererScenes.hpp"

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"

namespace RendererDemo
{
    namespace
    {
        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            // The scene owns what it loads, so its assets go when it is left.
            const Fenrir::LoadResult<Fenrir::Mesh> cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            if (!cube.Ok())
            {
                app.Logger()->Error("Meshes and materials: the cube did not load from the asset root");
                return;
            }
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene).handle;
            const Fenrir::AssetHandle<Fenrir::Material> material = store.CreateMaterial(lit, scene);
            store.SetColour(material, "baseColour", Fenrir::Colour::FromSrgb(0xC08040FF));

            Fenrir::EntityList& entities = scene.GetEntityList();
            Fenrir::Entity box = entities.CreateEntity();
            box.AddComponent<Fenrir::Transform>().rot =
                Fenrir::Math::AngleAxis(0.6f, Fenrir::Math::Vec3(0.70710678f, 0.70710678f, 0.0f));
            box.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{cube.handle, material});

            // The light shines along its -Z: turned down and to the side, so three faces of the cube differ.
            Fenrir::Entity light = entities.CreateEntity();
            light.AddComponent<Fenrir::Transform>().rot =
                Fenrir::Math::AngleAxis(0.5f, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f)) *
                Fenrir::Math::AngleAxis(-0.8f, Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f));
            light.AddComponent<Fenrir::DirectionalLight>();

            scene.AddResource<Fenrir::AmbientLight>();
        }
    } // namespace

    FenrirDemo::DemoScene MeshesAndMaterials()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Meshes and materials";
        scene.keyHelp = "none yet (renderer ticket 13 fills this scene)";
        scene.camera.position = {0.0f, 1.0f, 3.0f};
        scene.camera.pitch = -0.3f;
        scene.spawn = Spawn;
        return scene;
    }
} // namespace RendererDemo
