// Model import (ADR-0021, ADR-0024, ADR-0031): one scene written as glTF, GLB, FBX and OBJ by
// fixtures/models/make_fixtures.py (its docstring describes the scene) loads as the same node tree, meshes and material
// mapping, within what each format can say.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "AssetsTest.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirScene/Scene.hpp"

using AssetsTest::AssetsApp;
using Fenrir::AssetHandle;
using Fenrir::AssetOwner;

namespace
{
    using Type = Fenrir::ShaderParameterType;

    bool Near(float a, float b)
    {
        return std::abs(a - b) < 1e-4f;
    }

    bool Near(const Fenrir::Math::Vec3& a, const Fenrir::Math::Vec3& b)
    {
        return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
    }

    bool Near(const Fenrir::Math::Vec4& a, const Fenrir::Math::Vec4& b)
    {
        return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z) && Near(a.w, b.w);
    }

    // A quaternion and its negation are the same rotation.
    bool SameRotation(const Fenrir::Math::Quat& a, const Fenrir::Math::Quat& b)
    {
        return std::abs(std::abs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z) - 1.0f) < 1e-4f;
    }

    bool Contains(const std::vector<std::string>& messages, const std::string& part)
    {
        for (const std::string& message : messages)
        {
            if (message.find(part) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    // The scene's model file in each format, with what that format cannot say.
    struct Format
    {
        const char* file;
        // OBJ has no hierarchy or node transforms: only the two mesh nodes, at the origin.
        bool hierarchy = true;
        // Only glTF marks a material unlit, masked or double-sided; FBX's and OBJ's Glow is plain lit and opaque.
        bool unlitMaskAndDoubleSided = true;
        // FBX's Phong material has no metalness or roughness of its own: ufbx's defaults for it are written instead.
        bool metallicRoughness = true;
        // glTF and FBX wrap each axis of a texture; the MTL file gives no wrap, so OBJ's samplers are the default.
        bool wraps = true;
        // Only glTF gives a texture filter.
        bool filter = true;
    };

    const Format gltf{"scene.gltf"};
    const Format glb{"scene.glb"};
    const Format fbx{"scene.fbx", true, false, false, true, false};
    const Format obj{"scene.obj", false, false, true, false, false};

    const Fenrir::Model& LoadScene(AssetsApp& test, const char* file)
    {
        const Fenrir::LoadResult<Fenrir::Model> loaded =
            test.store.Load<Fenrir::Model>(std::string("models/") + file, AssetOwner::App());
        INFO("the model loads: " << (loaded.diagnostic ? loaded.diagnostic->importerMessage : std::string()));
        REQUIRE(loaded.Ok());
        const Fenrir::Model* model = test.store.TryGet(loaded.handle);
        REQUIRE(model != nullptr);
        return *model;
    }

    const Fenrir::ModelNode& FindNode(const Fenrir::Model& model, const std::string& name)
    {
        const auto found = std::find_if(model.nodes.begin(), model.nodes.end(),
                                        [&](const Fenrir::ModelNode& node) { return node.name == name; });
        INFO("the model has a node called " << name);
        REQUIRE(found != model.nodes.end());
        return *found;
    }

    // A stand-in for one of FenrirRenderer's built-in shaders: a container with the built-ins' standard parameters
    // and their defaults (ADR-0024), but no real code. The renderer registers the real ones; these let the store check
    // the imported overrides against a shader's parameters with no renderer.
    AssetHandle<Fenrir::Shader> RegisterStandIn(AssetsApp& test, const char* name, bool lit)
    {
        std::vector<Fenrir::ShaderParameter> parameters = {
            {"baseColour", Type::Vector4, {1.0f, 1.0f, 1.0f, 1.0f}},
            {"baseColourTexture", Type::Texture, {}, Fenrir::DefaultTexture::White},
        };
        if (lit)
        {
            parameters.push_back({"metallic", Type::Float, {0.0f}});
            parameters.push_back({"roughness", Type::Float, {0.5f}});
            parameters.push_back({"emissive", Type::Vector4, {0.0f, 0.0f, 0.0f, 1.0f}});
            parameters.push_back({"metallicRoughnessTexture", Type::Texture, {}, Fenrir::DefaultTexture::White});
            parameters.push_back({"normalTexture", Type::Texture, {}, Fenrir::DefaultTexture::Normal});
            parameters.push_back({"emissiveTexture", Type::Texture, {}, Fenrir::DefaultTexture::Black});
        }
        Fenrir::ShaderContainer container;
        container.name = name;
        Fenrir::ShaderVariant variant;
        variant.stages.push_back({Fenrir::ShaderStage::Vertex, {0x07230203u}, {0x07230203u}});
        variant.stages.push_back({Fenrir::ShaderStage::Fragment, {0x07230203u}, {0x07230203u}});
        variant.parameters = std::move(parameters);
        container.variants.push_back(std::move(variant));
        Fenrir::ShaderData data;
        data.bytes = Fenrir::WriteShaderContainer(container);
        return test.store.AddBuiltin<Fenrir::Shader>(name, std::move(data));
    }

    const Fenrir::MaterialData& MaterialOf(AssetsApp& test, const Fenrir::ModelNode& node)
    {
        REQUIRE(node.meshes.size() == 1);
        const Fenrir::MaterialData* material = test.store.TryGet(node.meshes[0].material);
        REQUIRE(material != nullptr);
        return *material;
    }
} // namespace

TEST_CASE("Each format of the scene loads as the same node tree", "[assets][models]")
{
    const Format format = GENERATE(gltf, glb, fbx, obj);
    CAPTURE(format.file);
    AssetsApp test;
    const Fenrir::Model& model = LoadScene(test, format.file);

    if (!format.hierarchy)
    {
        INFO("an OBJ file is a flat list of its objects");
        REQUIRE(model.nodes.size() == 2);
        REQUIRE(model.nodes[0].name == "Panel");
        REQUIRE(model.nodes[1].name == "Marker");
        REQUIRE(model.nodes[0].parent == -1);
        REQUIRE(model.nodes[1].parent == -1);
        REQUIRE(model.nodes[0].sourceId != model.nodes[1].sourceId);
        return;
    }

    INFO("the nodes come parents first, in the file's order: Root, Panel, Marker, Empty");
    REQUIRE(model.nodes.size() == 4);
    REQUIRE(model.nodes[0].name == "Root");
    REQUIRE(model.nodes[1].name == "Panel");
    REQUIRE(model.nodes[2].name == "Marker");
    REQUIRE(model.nodes[3].name == "Empty");

    INFO("each node names its parent by index");
    REQUIRE(model.nodes[0].parent == -1);
    REQUIRE(model.nodes[1].parent == 0);
    REQUIRE(model.nodes[2].parent == 0);
    REQUIRE(model.nodes[3].parent == 2);

    INFO("every node keeps a source identity of its own");
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        for (std::size_t j = i + 1; j < model.nodes.size(); ++j)
        {
            REQUIRE(model.nodes[i].sourceId != model.nodes[j].sourceId);
        }
    }

    INFO("Root's rest transform is T(1, 2, 3), 90 degrees about +Y as a unit quaternion, S(2, 2, 2)");
    const Fenrir::Transform& root = model.nodes[0].rest;
    REQUIRE(Near(root.pos, {1.0f, 2.0f, 3.0f}));
    REQUIRE(SameRotation(root.rot, Fenrir::Math::Quat(std::sqrt(0.5f), 0.0f, std::sqrt(0.5f), 0.0f)));
    REQUIRE(Near(root.scale, {2.0f, 2.0f, 2.0f}));

    INFO("the children keep their own translations");
    REQUIRE(Near(model.nodes[1].rest.pos, {0.0f, 1.0f, 0.0f}));
    REQUIRE(Near(model.nodes[2].rest.pos, {0.0f, 0.0f, -1.0f}));
    REQUIRE(Near(model.nodes[3].rest.pos, {0.5f, 0.0f, 0.0f}));
    REQUIRE(SameRotation(model.nodes[3].rest.rot, Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f)));
}

TEST_CASE("Each format of the scene gives the same meshes", "[assets][models]")
{
    const Format format = GENERATE(gltf, glb, fbx, obj);
    CAPTURE(format.file);
    AssetsApp test;
    const Fenrir::Model& model = LoadScene(test, format.file);

    INFO("two meshes, one on each mesh node");
    REQUIRE(model.meshes.size() == 2);
    const Fenrir::ModelNode& panelNode = FindNode(model, "Panel");
    const Fenrir::ModelNode& markerNode = FindNode(model, "Marker");
    REQUIRE(panelNode.meshes.size() == 1);
    REQUIRE(markerNode.meshes.size() == 1);

    const Fenrir::MeshData* panel = test.store.TryGet(panelNode.meshes[0].mesh);
    REQUIRE(panel != nullptr);
    INFO("the panel is a quad: four vertices, two triangles in one submesh");
    REQUIRE(panel->positions.size() == 4);
    REQUIRE(panel->indices.size() == 6);
    REQUIRE(panel->submeshes.size() == 1);
    REQUIRE(panel->submeshes[0].indexCount == 6);
    INFO("its bounds run from (-1, -1, 0) to (1, 1, 0)");
    REQUIRE(Near(panel->bounds.min, {-1.0f, -1.0f, 0.0f}));
    REQUIRE(Near(panel->bounds.max, {1.0f, 1.0f, 0.0f}));
    INFO("its normals face +Z, as the file says");
    REQUIRE(panel->normals.size() == 4);
    for (const Fenrir::Math::Vec3& normal : panel->normals)
    {
        REQUIRE(Near(normal, {0.0f, 0.0f, 1.0f}));
    }
    INFO("UV (0, 0) is the image's top-left in every format: the bottom-left corner has V 1");
    REQUIRE(panel->uv0.size() == 4);
    for (std::size_t i = 0; i < panel->positions.size(); ++i)
    {
        if (Near(panel->positions[i], {-1.0f, -1.0f, 0.0f}))
        {
            REQUIRE(Near(panel->uv0[i].x, 0.0f));
            REQUIRE(Near(panel->uv0[i].y, 1.0f));
        }
    }

    const Fenrir::MeshData* marker = test.store.TryGet(markerNode.meshes[0].mesh);
    REQUIRE(marker != nullptr);
    INFO("the marker triangle has no normals in its file, so they are generated, facing +Z");
    REQUIRE(marker->positions.size() == 3);
    REQUIRE(marker->normals.size() == 3);
    for (const Fenrir::Math::Vec3& normal : marker->normals)
    {
        REQUIRE(Near(normal, {0.0f, 0.0f, 1.0f}));
    }
}

TEST_CASE("A model's tangents and their handedness are kept, and a mesh without them has no tangent stream",
          "[assets][models]")
{
    // fixtures/models/make_fixtures.py's write_tangents: glTF writes the handedness in w, FBX a binormal it comes from.
    const char* file = GENERATE("tangents.gltf", "tangents.fbx");
    CAPTURE(file);
    AssetsApp test;
    const Fenrir::Model& model = LoadScene(test, file);
    const auto meshOf = [&](const char* name) -> const Fenrir::MeshData& {
        const Fenrir::ModelNode& node = FindNode(model, name);
        REQUIRE(node.meshes.size() == 1);
        const Fenrir::MeshData* mesh = test.store.TryGet(node.meshes[0].mesh);
        REQUIRE(mesh != nullptr);
        return *mesh;
    };

    const Fenrir::MeshData& plain = meshOf("Plain");
    INFO("Plain's tangents run along +X with handedness +1, one per vertex");
    REQUIRE(plain.tangents.size() == plain.positions.size());
    for (const Fenrir::Math::Vec4& tangent : plain.tangents)
    {
        REQUIRE(Near(tangent, {1.0f, 0.0f, 0.0f, 1.0f}));
    }

    const Fenrir::MeshData& mirrored = meshOf("Mirrored");
    INFO("Mirrored's U runs the other way: its tangents run along -X with handedness -1");
    REQUIRE(mirrored.tangents.size() == mirrored.positions.size());
    for (const Fenrir::Math::Vec4& tangent : mirrored.tangents)
    {
        REQUIRE(Near(tangent, {-1.0f, 0.0f, 0.0f, -1.0f}));
    }

    INFO("Bare has normals and UVs but no tangents in its file, and none are generated");
    const Fenrir::MeshData& bare = meshOf("Bare");
    REQUIRE(bare.normals.size() == bare.positions.size());
    REQUIRE(bare.uv0.size() == bare.positions.size());
    REQUIRE(bare.tangents.empty());
}

TEST_CASE("Each format of the scene maps its materials onto the built-ins", "[assets][models][materials]")
{
    const Format format = GENERATE(gltf, glb, fbx, obj);
    CAPTURE(format.file);
    AssetsApp test;
    const AssetHandle<Fenrir::Shader> lit = RegisterStandIn(test, "builtin:lit", true);
    const AssetHandle<Fenrir::Shader> unlit = RegisterStandIn(test, "builtin:unlit", false);
    const Fenrir::Model& model = LoadScene(test, format.file);

    const Fenrir::ModelNode& panelNode = FindNode(model, "Panel");
    const AssetHandle<Fenrir::Material> painted = panelNode.meshes.at(0).material;
    const Fenrir::MaterialData& paintedData = MaterialOf(test, panelNode);

    INFO("Painted is lit and blended, its alpha coming from the file's opacity");
    REQUIRE(paintedData.shader == lit);
    REQUIRE(paintedData.blendMode == Fenrir::BlendMode::AlphaBlend);
    REQUIRE(paintedData.doubleSided == format.unlitMaskAndDoubleSided);

    INFO("every factor the file gives is written as an override, and read back through the shader");
    REQUIRE(Near(test.store.TryGetVector4(painted, "baseColour").value(), {0.8f, 0.6f, 0.4f, 0.5f}));
    REQUIRE(Near(test.store.TryGetVector4(painted, "emissive").value(), {0.1f, 0.2f, 0.3f, 1.0f}));
    REQUIRE(test.store.IsSet(painted, "metallic"));
    REQUIRE(test.store.IsSet(painted, "roughness"));
    if (format.metallicRoughness)
    {
        REQUIRE(Near(test.store.TryGetFloat(painted, "metallic").value(), 0.25f));
        REQUIRE(Near(test.store.TryGetFloat(painted, "roughness").value(), 0.75f));
    }

    INFO("the base colour texture is the 2x2 checker, and the normal map the 1x1 normal image");
    const std::optional<Fenrir::MaterialTexture> base = test.store.TryGetTexture(painted, "baseColourTexture");
    const std::optional<Fenrir::MaterialTexture> normal = test.store.TryGetTexture(painted, "normalTexture");
    REQUIRE(base.has_value());
    REQUIRE(normal.has_value());
    const Fenrir::ImageData* checker = test.store.TryGet(base->image);
    const Fenrir::ImageData* bumps = test.store.TryGet(normal->image);
    REQUIRE(checker != nullptr);
    REQUIRE(bumps != nullptr);
    REQUIRE(checker->width == 2);
    REQUIRE(bumps->width == 1);
    REQUIRE(model.images.size() == 2);

    INFO("the sampler comes from the file: clamped across, repeating down");
    if (format.wraps)
    {
        REQUIRE(base->sampler.wrapU == Fenrir::SamplerWrap::ClampToEdge);
        REQUIRE(base->sampler.wrapV == Fenrir::SamplerWrap::Repeat);
    }
    else
    {
        REQUIRE(base->sampler == Fenrir::SamplerDescription{});
    }
    if (format.filter)
    {
        REQUIRE(base->sampler.filter == Fenrir::SamplerFilter::Nearest);
        INFO("a texture whose file gives no sampler reads the default one");
        REQUIRE(normal->sampler == Fenrir::SamplerDescription{});
    }

    const Fenrir::ModelNode& markerNode = FindNode(model, "Marker");
    const AssetHandle<Fenrir::Material> glow = markerNode.meshes.at(0).material;
    const Fenrir::MaterialData& glowData = MaterialOf(test, markerNode);
    INFO("Glow's base colour is green in every format");
    REQUIRE(Near(test.store.TryGetVector4(glow, "baseColour").value(), {0.0f, 1.0f, 0.0f, 1.0f}));
    if (format.unlitMaskAndDoubleSided)
    {
        INFO("glTF marks Glow unlit and masked at 0.3, so it draws with builtin:unlit and writes no lit factors");
        REQUIRE(glowData.shader == unlit);
        REQUIRE(glowData.blendMode == Fenrir::BlendMode::Masked);
        REQUIRE(Near(glowData.alphaCutoff, 0.3f));
        REQUIRE_FALSE(test.store.IsSet(glow, "metallic"));
    }
    else
    {
        INFO("FBX and OBJ cannot mark it unlit or masked: it is lit and opaque");
        REQUIRE(glowData.shader == lit);
        REQUIRE(glowData.blendMode == Fenrir::BlendMode::Opaque);
    }
    INFO("nothing was warned: every override is one of the built-ins' parameters");
    REQUIRE(test.warnings->empty());
}

TEST_CASE("glTF's own defaults are written for factors a file leaves out", "[assets][models][materials]")
{
    AssetsApp test;
    const Fenrir::Model& model = LoadScene(test, "missing-texture.gltf");
    const AssetHandle<Fenrir::Material> lost = model.materials.at(0);

    INFO("a glTF material giving no factors keeps glTF's look: white, metallic 1, roughness 1, no emission");
    REQUIRE(Near(test.store.TryGetOverrideVector4(lost, "baseColour").value(), {1.0f, 1.0f, 1.0f, 1.0f}));
    REQUIRE(Near(test.store.TryGetOverrideFloat(lost, "metallic").value(), 1.0f));
    REQUIRE(Near(test.store.TryGetOverrideFloat(lost, "roughness").value(), 1.0f));
    REQUIRE(Near(test.store.TryGetOverrideVector4(lost, "emissive").value(), {0.0f, 0.0f, 0.0f, 1.0f}));
}

TEST_CASE("Imported materials name the built-ins before the renderer registers them", "[assets][models][materials]")
{
    AssetsApp test;
    const Fenrir::Model& model = LoadScene(test, "scene.gltf");
    const AssetHandle<Fenrir::Material> painted = model.materials.at(0);
    const Fenrir::MaterialData* data = test.store.TryGet(painted);
    REQUIRE(data != nullptr);

    INFO("before builtin:lit is registered, the material's shader does not resolve, and the name does not load");
    REQUIRE_FALSE(test.store.IsValid(data->shader));
    REQUIRE_FALSE(test.store.TryGetFloat(painted, "metallic").has_value());
    const Fenrir::LoadResult<Fenrir::Shader> early = test.store.Load<Fenrir::Shader>("builtin:lit", AssetOwner::App());
    REQUIRE(early.diagnostic.has_value());
    REQUIRE(early.diagnostic->reason == Fenrir::LoadFailure::NotRegistered);

    const AssetHandle<Fenrir::Shader> lit = RegisterStandIn(test, "builtin:lit", true);
    INFO("registering it fills the handle the material already holds");
    REQUIRE(data->shader == lit);
    REQUIRE(test.store.IsValid(lit));
    REQUIRE(Near(test.store.TryGetFloat(painted, "metallic").value(), 0.25f));
    INFO("and loading the name now gives that handle");
    REQUIRE(test.store.Load<Fenrir::Shader>("builtin:lit", AssetOwner::App()).handle == lit);
}

TEST_CASE("A built-in needs the builtin: prefix", "[assets][models][materials]")
{
    AssetsApp test;
    Fenrir::ShaderData data;
    data.bytes = Fenrir::WriteShaderContainer(Fenrir::ShaderContainer{});
    const AssetHandle<Fenrir::Shader> shader = test.store.AddBuiltin<Fenrir::Shader>("lit", std::move(data));

    INFO("a name without the prefix is refused with a logged error");
    REQUIRE(shader.IsEmpty());
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("Image colour spaces follow their first use", "[assets][models][images]")
{
    AssetsApp test;
    const Fenrir::Model& scene = LoadScene(test, "scene.gltf");

    INFO("the base colour texture is sRGB and the normal map linear");
    REQUIRE(test.store.TryGet(scene.images.at(0))->colourSpace == Fenrir::ColourSpace::Srgb);
    REQUIRE(test.store.TryGet(scene.images.at(1))->colourSpace == Fenrir::ColourSpace::Linear);
    REQUIRE(test.warnings->empty());

    const Fenrir::Model& conflict = LoadScene(test, "conflict.gltf");
    INFO("an image used first as a base colour and then as a normal map stays sRGB, with one warning");
    REQUIRE(conflict.images.size() == 1);
    REQUIRE(test.store.TryGet(conflict.images[0])->colourSpace == Fenrir::ColourSpace::Srgb);
    REQUIRE(test.warnings->size() == 1);
    REQUIRE(Contains(*test.warnings, "Bumps"));
}

TEST_CASE("A texture that cannot be loaded is replaced by a white image with a warning", "[assets][models][images]")
{
    AssetsApp test;
    const Fenrir::Model& model = LoadScene(test, "missing-texture.gltf");

    INFO("the model still loads, its image a 1x1 white pixel");
    REQUIRE(model.images.size() == 1);
    const Fenrir::ImageData* image = test.store.TryGet(model.images[0]);
    REQUIRE(image != nullptr);
    REQUIRE(image->width == 1);
    REQUIRE(image->height == 1);
    REQUIRE(image->pixels == std::vector<std::byte>(4, std::byte{0xff}));

    INFO("the material uses it as its base colour texture");
    REQUIRE(test.store.TryGetOverrideTexture(model.materials.at(0), "baseColourTexture")->image == model.images[0]);

    INFO("one warning names the missing file, and nothing is an error");
    REQUIRE(test.warnings->size() == 1);
    REQUIRE(Contains(*test.warnings, "absent.png"));
    REQUIRE(test.errors->empty());
}

TEST_CASE("A skinned glTF mesh is imported with a skin section baked against its skeleton", "[assets][models]")
{
    AssetsApp test;
    const Fenrir::Model& model = LoadScene(test, "skinned.gltf");

    INFO("every node is kept, the joint included");
    REQUIRE(model.nodes.size() == 4);
    INFO("the skinned mesh and the static mesh are both imported");
    REQUIRE(model.meshes.size() == 2);
    REQUIRE(model.skeletons.size() == 1);
    const Fenrir::SkeletonData* skeleton = test.store.TryGet(model.skeletons[0]);
    REQUIRE(skeleton != nullptr);
    REQUIRE(skeleton->BoneCount() == 1);

    const Fenrir::ModelNode& body = FindNode(model, "Body");
    REQUIRE(body.meshes.size() == 1);
    INFO("the skinned mesh names the skeleton it was baked against");
    REQUIRE(body.meshes[0].skeleton == 0);
    const Fenrir::MeshData* mesh = test.store.TryGet(body.meshes[0].mesh);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->skin.has_value());
    INFO("every vertex is weighted wholly to the one joint");
    REQUIRE(mesh->skin->joints == std::vector<Fenrir::BoneQuad>(3, {0, 0, 0, 0}));
    REQUIRE(mesh->skin->weights == std::vector<Fenrir::BoneQuad>(3, {65535, 0, 0, 0}));
    INFO("its expected signature is the skeleton's bind signature");
    REQUIRE(mesh->skin->expectedSignature == skeleton->BindSignature());
    REQUIRE(mesh->skin->expectedSignature != 0);

    INFO("the rigid mesh names no skeleton and has no skin");
    const Fenrir::ModelNode& prop = FindNode(model, "Prop");
    REQUIRE(prop.meshes.size() == 1);
    REQUIRE(prop.meshes[0].skeleton == -1);
    REQUIRE_FALSE(test.store.TryGet(prop.meshes[0].mesh)->skin.has_value());

    INFO("the joint is bone 0 of skeleton 0, and Rig, its parent, is the skeleton's owner");
    REQUIRE(FindNode(model, "Bone").kind == Fenrir::ModelNodeKind::Bone);
    REQUIRE(FindNode(model, "Bone").skeleton == 0);
    REQUIRE(FindNode(model, "Bone").bone == 0);
    REQUIRE(model.skeletonOwners == std::vector<std::int32_t>{0});
    REQUIRE(model.nodes[0].name == "Rig");
    INFO("nothing is warned");
    REQUIRE(test.warnings->empty());
}

TEST_CASE("A sub-asset loads by label with the same handle the model holds", "[assets][models][loading]")
{
    AssetsApp test;
    const Fenrir::Model& model = LoadScene(test, "scene.gltf");

    INFO("Mesh, Material and Image labels number the model's lists");
    REQUIRE(test.store.Load<Fenrir::Mesh>("models/scene.gltf#Mesh1", AssetOwner::App()).handle == model.meshes[1]);
    REQUIRE(test.store.Load<Fenrir::Material>("models/scene.gltf#Material0", AssetOwner::App()).handle ==
            model.materials[0]);
    REQUIRE(test.store.Load<Fenrir::Image>("models/scene.gltf#Image1", AssetOwner::App()).handle == model.images[1]);
}

TEST_CASE("A sub-asset loaded first imports its model once", "[assets][models][loading]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Mesh> mesh =
        test.store.Load<Fenrir::Mesh>("models/scene.glb#Mesh0", AssetOwner::App());
    REQUIRE(mesh.Ok());

    const Fenrir::Model& model = LoadScene(test, "scene.glb");
    INFO("loading the model afterwards gives the mesh already loaded");
    REQUIRE(model.meshes.at(0) == mesh.handle);
}

TEST_CASE("A label the model lacks, or a sub-asset with no label, fails with a diagnostic", "[assets][models][loading]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Mesh> unknown =
        test.store.Load<Fenrir::Mesh>("models/scene.gltf#Mesh9", AssetOwner::App());
    INFO("the model has two meshes, so Mesh9 is not found");
    REQUIRE(unknown.diagnostic.has_value());
    REQUIRE(unknown.diagnostic->reason == Fenrir::LoadFailure::LabelNotFound);

    const Fenrir::LoadResult<Fenrir::Mesh> unlabelled =
        test.store.Load<Fenrir::Mesh>("models/scene.gltf", AssetOwner::App());
    INFO("a mesh from a model file needs a label");
    REQUIRE(unlabelled.diagnostic.has_value());
    REQUIRE(unlabelled.diagnostic->reason == Fenrir::LoadFailure::LabelNotFound);
}

TEST_CASE("A model's sub-assets are owned by whoever loads the model", "[assets][models][owners]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Fenrir::Model> forLevel = test.store.Load<Fenrir::Model>("models/scene.gltf", level);
    REQUIRE(forLevel.Ok());
    const Fenrir::Model model = *test.store.TryGet(forLevel.handle);

    SECTION("a scene's model and its sub-assets end with the scene")
    {
        test.app.DestroyScene("Level");
        REQUIRE_FALSE(test.store.IsValid(forLevel.handle));
        REQUIRE_FALSE(test.store.IsValid(model.meshes[0]));
        REQUIRE_FALSE(test.store.IsValid(model.materials[0]));
        REQUIRE_FALSE(test.store.IsValid(model.images[0]));
    }
    SECTION("loading the model for the app too keeps its sub-assets after the scene ends")
    {
        REQUIRE(test.store.Load<Fenrir::Model>("models/scene.gltf", AssetOwner::App()).handle == forLevel.handle);
        test.app.DestroyScene("Level");
        REQUIRE(test.store.IsValid(forLevel.handle));
        REQUIRE(test.store.IsValid(model.meshes[0]));
        REQUIRE(test.store.IsValid(model.materials[0]));
        REQUIRE(test.store.IsValid(model.images[0]));
    }
}

TEST_CASE("A model file the importer cannot read fails the load with its message", "[assets][models][loading]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Model> broken =
        test.store.Load<Fenrir::Model>("models/corrupt.gltf", AssetOwner::App());

    INFO("the import fails, with cgltf's reason in the diagnostic");
    REQUIRE(broken.diagnostic.has_value());
    REQUIRE(broken.diagnostic->reason == Fenrir::LoadFailure::ImportFailed);
    REQUIRE_FALSE(broken.diagnostic->importerMessage.empty());
    REQUIRE(test.errors->size() == 1);
}
