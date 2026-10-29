// Skinned glTF import (ADR-0031, ADR-0035): skeletons, owners, one bind space, clips and partial loads, on the skinned
// fixtures fixtures/models/make_fixtures.py writes (each function's docstring there describes its file). Expected
// values are the fixtures' own numbers, or glTF's skinning formula worked from them, never the importer's code.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "AssetsTest.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScene/Scene.hpp"

using AssetsTest::AssetsApp;
using Fenrir::AssetOwner;
using Fenrir::ModelNodeKind;
using Fenrir::Math::Mat4;
using Fenrir::Math::Quat;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;

namespace
{
    struct Loaded
    {
        Fenrir::LoadResult<Fenrir::Model> result;
        const Fenrir::Model* model = nullptr;
    };

    Loaded Load(AssetsApp& test, const std::string& file)
    {
        Loaded loaded{test.store.Load<Fenrir::Model>("models/" + file, AssetOwner::App())};
        INFO("the model loads: " << (loaded.result.diagnostic ? loaded.result.diagnostic->importerMessage : ""));
        REQUIRE(loaded.result.Ok());
        loaded.model = test.store.TryGet(loaded.result.handle);
        REQUIRE(loaded.model != nullptr);
        return loaded;
    }

    std::int32_t FindNode(const Fenrir::Model& model, const std::string& name)
    {
        const auto found = std::find_if(model.nodes.begin(), model.nodes.end(),
                                        [&](const Fenrir::ModelNode& node) { return node.name == name; });
        INFO("the model has a node called " << name);
        REQUIRE(found != model.nodes.end());
        return static_cast<std::int32_t>(found - model.nodes.begin());
    }

    const Fenrir::ModelNode& NodeNamed(const Fenrir::Model& model, const std::string& name)
    {
        return model.nodes[static_cast<std::size_t>(FindNode(model, name))];
    }

    const Fenrir::SkeletonData& SkeletonOf(AssetsApp& test, const Fenrir::Model& model, std::size_t k)
    {
        REQUIRE(k < model.skeletons.size());
        const Fenrir::SkeletonData* skeleton = test.store.TryGet(model.skeletons[k]);
        REQUIRE(skeleton != nullptr);
        return *skeleton;
    }

    const Fenrir::MeshData& MeshOn(AssetsApp& test, const Fenrir::Model& model, const std::string& node)
    {
        const Fenrir::ModelNode& found = NodeNamed(model, node);
        INFO("node " << node << " draws one mesh");
        REQUIRE(found.meshes.size() == 1);
        const Fenrir::MeshData* mesh = test.store.TryGet(found.meshes[0].mesh);
        REQUIRE(mesh != nullptr);
        return *mesh;
    }

    bool Near(float a, float b, float tolerance = 1e-4f)
    {
        return std::abs(a - b) <= tolerance;
    }

    bool Near(const Vec3& a, const Vec3& b, float tolerance = 1e-4f)
    {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
    }

    bool Near(const Vec4& a, const Vec4& b, float tolerance = 1e-4f)
    {
        return Near(Vec3(a), Vec3(b), tolerance) && Near(a.w, b.w, tolerance);
    }

    bool Near(const Mat4& a, const Mat4& b, float tolerance = 1e-4f)
    {
        for (glm::length_t c = 0; c < 4; ++c)
        {
            if (!Near(a[c], b[c], tolerance))
            {
                return false;
            }
        }
        return true;
    }

    Mat4 TRS(Vec3 t, Quat r = Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3 s = Vec3(1.0f))
    {
        return glm::translate(Mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(Mat4(1.0f), s);
    }

    // glTF writes x, y, z, w; this is `degrees` about `axis` in glm's order.
    Quat AxisAngle(Vec3 axis, float degrees)
    {
        return glm::angleAxis(glm::radians(degrees), glm::normalize(axis));
    }

    // Each bone's model matrix at the rest pose: the rest transforms composed down the skeleton.
    std::vector<Mat4> RestModelMatrices(const Fenrir::SkeletonData& skeleton)
    {
        std::vector<Mat4> matrices;
        for (const Fenrir::SkeletonBone& bone : skeleton.bones)
        {
            const Mat4 local = Fenrir::TransformToMat4(bone.rest);
            matrices.push_back(bone.parent >= 0 ? matrices[static_cast<std::size_t>(bone.parent)] * local : local);
        }
        return matrices;
    }

    // The node's rest world: its rest transforms composed from the model's root.
    Mat4 NodeWorld(const Fenrir::Model& model, std::int32_t node)
    {
        Mat4 world(1.0f);
        for (; node >= 0; node = model.nodes[static_cast<std::size_t>(node)].parent)
        {
            world = Fenrir::TransformToMat4(model.nodes[static_cast<std::size_t>(node)].rest) * world;
        }
        return world;
    }

    // Linear blend skinning of vertex `v` in skeleton bind space with the palette `model x inverse bind`.
    Vec3 Skin(const Fenrir::MeshData& mesh, std::size_t v, const Fenrir::SkeletonData& skeleton,
              const std::vector<Mat4>& modelMatrices)
    {
        Vec3 skinned(0.0f);
        for (std::size_t i = 0; i < 4; ++i)
        {
            const float weight = static_cast<float>(mesh.skin->weights[v][i]) / 65535.0f;
            const std::size_t bone = mesh.skin->joints[v][i];
            const Mat4 palette = modelMatrices[bone] * skeleton.bones[bone].inverseBind;
            skinned += weight * Vec3(palette * Vec4(mesh.positions[v], 1.0f));
        }
        return skinned;
    }

    std::vector<std::string> EngineNames(const Fenrir::SkeletonData& skeleton)
    {
        std::vector<std::string> names;
        for (std::size_t i = 0; i < skeleton.BoneCount(); ++i)
        {
            names.emplace_back(skeleton.EngineName(static_cast<std::int32_t>(i)));
        }
        return names;
    }

    // What a load left out, for a failure message.
    std::string Joined(const std::vector<std::string>& messages)
    {
        std::string joined;
        for (const std::string& message : messages)
        {
            joined += message + "; ";
        }
        return joined;
    }

    bool Contains(const std::vector<std::string>& messages, const std::string& part)
    {
        return std::any_of(messages.begin(), messages.end(),
                           [&](const std::string& message) { return message.find(part) != std::string::npos; });
    }
} // namespace

TEST_CASE("Every glTF fixture whose rest is its bind reconstructs its bind pose", "[assets][skinned-import]")
{
    const std::string file =
        GENERATE("armatures.gltf", "shared-skins.gltf", "incompatible-skin.gltf", "cubic-clip.gltf");
    AssetsApp test;
    const Loaded loaded = Load(test, file);
    const Fenrir::Model& model = *loaded.model;
    REQUIRE_FALSE(model.skeletons.empty());

    std::vector<std::vector<Mat4>> matrices;
    for (std::size_t k = 0; k < model.skeletons.size(); ++k)
    {
        const Fenrir::SkeletonData& skeleton = SkeletonOf(test, model, k);
        matrices.push_back(RestModelMatrices(skeleton));
        for (std::size_t i = 0; i < skeleton.BoneCount(); ++i)
        {
            INFO(file << ": skeleton " << k << "'s bone " << skeleton.EngineName(static_cast<std::int32_t>(i))
                      << " has M_model_bind x B = I");
            REQUIRE(Near(matrices[k][i] * skeleton.bones[i].inverseBind, Mat4(1.0f)));
        }
    }
    for (const Fenrir::ModelNode& node : model.nodes)
    {
        for (const Fenrir::ModelMesh& mesh : node.meshes)
        {
            if (mesh.skeleton < 0)
            {
                continue;
            }
            const Fenrir::MeshData& data = *test.store.TryGet(mesh.mesh);
            const auto k = static_cast<std::size_t>(mesh.skeleton);
            for (std::size_t v = 0; v < data.positions.size(); ++v)
            {
                INFO(file << ": mesh on " << node.name << ", vertex " << v
                          << ", skinned at the bind pose, is where it was baked");
                REQUIRE(Near(Skin(data, v, SkeletonOf(test, model, k), matrices[k]), data.positions[v]));
            }
        }
    }
}

TEST_CASE("A skin with no inverse-bind accessor binds with identity matrices", "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "skinned.gltf");
    const Fenrir::SkeletonData& skeleton = SkeletonOf(test, *loaded.model, 0);
    INFO("the bind pose is the identity, though the rest pose is not");
    REQUIRE(Near(skeleton.bones[0].inverseBind, Mat4(1.0f)));
    REQUIRE(Near(skeleton.bones[0].rest.pos, Vec3(0.0f, 1.0f, 0.0f)));
}

TEST_CASE("Unrelated armatures are separate skeletons, with synthetic owners where they share a node",
          "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "armatures.gltf");
    const Fenrir::Model& model = *loaded.model;
    INFO("nothing failed");
    INFO("left out: " << Joined(loaded.result.failedUnits));
    REQUIRE(loaded.result.Status() == Fenrir::LoadStatus::Succeeded);

    INFO("three skins share no joints: three skeletons");
    REQUIRE(model.skeletons.size() == 3);
    REQUIRE(model.skeletonOwners.size() == 3);

    INFO("HumanSkin and TwinSkin both resolve to Scene, so each gets a synthetic owner named after its first root");
    const std::int32_t scene = FindNode(model, "Scene");
    for (std::size_t k = 0; k < 2; ++k)
    {
        const Fenrir::ModelNode& owner = model.nodes[static_cast<std::size_t>(model.skeletonOwners[k])];
        REQUIRE(owner.kind == ModelNodeKind::SyntheticOwner);
        REQUIRE(owner.parent == scene);
        REQUIRE(Near(Fenrir::TransformToMat4(owner.rest), Mat4(1.0f)));
        REQUIRE(owner.meshes.empty());
    }
    REQUIRE(model.nodes[static_cast<std::size_t>(model.skeletonOwners[0])].name == "Hips.Skeleton");
    REQUIRE(model.nodes[static_cast<std::size_t>(model.skeletonOwners[1])].name == "Pelvis.Skeleton");
    INFO("PropSkin's root is alone under Rig, which owns it");
    REQUIRE(model.skeletonOwners[2] == FindNode(model, "Rig"));
    REQUIRE(NodeNamed(model, "Rig").kind == ModelNodeKind::Ordinary);
    INFO("no two synthetic owners share an identity, nor one with a file node");
    REQUIRE(model.nodes[static_cast<std::size_t>(model.skeletonOwners[0])].sourceId !=
            model.nodes[static_cast<std::size_t>(model.skeletonOwners[1])].sourceId);
    REQUIRE(model.nodes[static_cast<std::size_t>(model.skeletonOwners[0])].sourceId >= model.nodes.size());
}

TEST_CASE("Bones are joints, intermediate nodes and animated empty children of bones", "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "armatures.gltf");
    const Fenrir::Model& model = *loaded.model;
    const Fenrir::SkeletonData& human = SkeletonOf(test, model, 0);

    INFO("skin order, joint order, parents first: Spine comes before Chest; Tail, no joint, comes last");
    REQUIRE(EngineNames(human) == std::vector<std::string>{"Hips", "Spine", "Chest", "Head", "Tail"});
    REQUIRE(human.Parent(1) == 0);
    REQUIRE(human.Parent(2) == 1);
    REQUIRE(human.Parent(4) == 2);

    INFO("each bone's node says so, and points back at its bone");
    for (std::size_t i = 0; i < human.BoneCount(); ++i)
    {
        const Fenrir::ModelNode& node = model.nodes[static_cast<std::size_t>(human.bones[i].modelNode)];
        REQUIRE(node.kind == ModelNodeKind::Bone);
        REQUIRE(node.skeleton == 0);
        REQUIRE(node.bone == static_cast<std::int32_t>(i));
        REQUIRE(human.bones[i].sourceId == node.sourceId);
    }

    INFO("Head is a joint no vertex is weighted to, and still a bone");
    REQUIRE(NodeNamed(model, "Head").kind == ModelNodeKind::Bone);
    INFO("an empty child of a bone that nothing animates is an ordinary node: an attachment");
    REQUIRE(NodeNamed(model, "Socket").kind == ModelNodeKind::Ordinary);
    REQUIRE(NodeNamed(model, "Socket").parent == FindNode(model, "Chest"));
    INFO("an animated child of a bone that carries a mesh is an ordinary node, its mesh kept");
    REQUIRE(NodeNamed(model, "Hat").kind == ModelNodeKind::Ordinary);
    REQUIRE(NodeNamed(model, "Hat").meshes.size() == 1);
    REQUIRE(NodeNamed(model, "Hat").meshes[0].skeleton == -1);
    INFO("the skinned mesh nodes are ordinary nodes");
    REQUIRE(NodeNamed(model, "BodyA").kind == ModelNodeKind::Ordinary);
}

TEST_CASE("Meshes drawn with one skin share its skeleton", "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "armatures.gltf");
    const Fenrir::Model& model = *loaded.model;
    REQUIRE(NodeNamed(model, "BodyA").meshes.at(0).skeleton == 0);
    REQUIRE(NodeNamed(model, "BodyB").meshes.at(0).skeleton == 0);
    REQUIRE(NodeNamed(model, "Legs").meshes.at(0).skeleton == 1);
    INFO("both expect skeleton 0's signature");
    const std::uint64_t signature = SkeletonOf(test, model, 0).BindSignature();
    REQUIRE(MeshOn(test, model, "BodyA").skin->expectedSignature == signature);
    REQUIRE(MeshOn(test, model, "BodyB").skin->expectedSignature == signature);

    INFO(
        "BodyA's vertex 1 is split evenly between Hips (bone 0) and Chest (bone 2), remapped from skin joints 0 and 1");
    const Fenrir::MeshSkin& skin = *MeshOn(test, model, "BodyA").skin;
    REQUIRE(skin.joints[1][0] == 0);
    REQUIRE(skin.joints[1][1] == 2);
    REQUIRE(skin.weights[1] == Fenrir::BoneQuad{32768, 32767, 0, 0});
}

TEST_CASE("A repeated bone name gets a suffixed engine name, and the load still succeeds", "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "armatures.gltf");
    const Fenrir::SkeletonData& twin = SkeletonOf(test, *loaded.model, 1);
    REQUIRE(EngineNames(twin) == std::vector<std::string>{"Pelvis", "Pelvis#2"});
    INFO("both keep their source name and their own node's identity");
    REQUIRE(twin.bones[1].sourceName == "Pelvis");
    REQUIRE(twin.bones[0].sourceId != twin.bones[1].sourceId);
    INFO("a warning says so");
    REQUIRE(Contains(*test.warnings, "Pelvis#2"));
    INFO("left out: " << Joined(loaded.result.failedUnits));
    REQUIRE(loaded.result.Status() == Fenrir::LoadStatus::Succeeded);
}

TEST_CASE("An owner that is not the identity converts binds and vertices into its space", "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "armatures.gltf");
    const Fenrir::Model& model = *loaded.model;
    const Fenrir::MeshData& prop = MeshOn(test, model, "Prop");
    const Mat4 owner = NodeWorld(model, model.skeletonOwners[2]);
    INFO("Rig is at (-3, 0, 0), so the owner's world takes the baked vertices back to the file's");
    REQUIRE(Near(Vec3(owner * Vec4(prop.positions[0], 1.0f)), Vec3(-3.0f, 1.0f, 0.0f)));
    REQUIRE(Near(Vec3(owner * Vec4(prop.positions[1], 1.0f)), Vec3(-2.0f, 1.0f, 0.0f)));
    REQUIRE(Near(prop.positions[0], Vec3(0.0f, 1.0f, 0.0f)));
}

TEST_CASE("Clip channels name their targets, and those on ordinary nodes are kept but not played",
          "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "armatures.gltf");
    const Fenrir::Model& model = *loaded.model;
    REQUIRE(model.clips.size() == 1);
    const Fenrir::ClipData* clip = test.store.TryGet(model.clips[0]);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->channels.size() == 3);
    REQUIRE(clip->duration == 1.0f);

    const auto channelOn = [&](const std::string& name) -> const Fenrir::ClipChannel& {
        const auto found = std::find_if(clip->channels.begin(), clip->channels.end(),
                                        [&](const Fenrir::ClipChannel& c) { return c.targetName == name; });
        INFO("a channel targets " << name);
        REQUIRE(found != clip->channels.end());
        return *found;
    };
    INFO("Spine and Tail are bones: their channels play, bound by identity and engine name");
    REQUIRE(channelOn("Spine").playable);
    REQUIRE(channelOn("Tail").playable);
    REQUIRE(channelOn("Tail").targetSourceId == NodeNamed(model, "Tail").sourceId);
    REQUIRE(channelOn("Tail").path == Fenrir::ClipPath::Rotation);
    INFO("Hat is an ordinary node: its channel is kept, unplayable, not redirected");
    REQUIRE_FALSE(channelOn("Hat").playable);
    REQUIRE(channelOn("Hat").path == Fenrir::ClipPath::Translation);
    REQUIRE(Near(channelOn("Hat").values[1], Vec4(0.0f, 0.8f, 0.0f, 0.0f)));
}

TEST_CASE("Skins sharing joints merge into one skeleton, through a conversion when their binds differ by one",
          "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "shared-skins.gltf");
    const Fenrir::Model& model = *loaded.model;
    INFO("left out: " << Joined(loaded.result.failedUnits));
    REQUIRE(loaded.result.Status() == Fenrir::LoadStatus::Succeeded);
    REQUIRE(model.skeletons.size() == 1);
    REQUIRE(EngineNames(SkeletonOf(test, model, 0)) == std::vector<std::string>{"Hip", "Knee", "Foot"});
    for (const char* node : {"Thigh", "Shin", "Boot"})
    {
        INFO(node << " is drawn with the one skeleton");
        REQUIRE(NodeNamed(model, node).meshes.at(0).skeleton == 0);
    }

    INFO("Boot's vertices were written C^-1 from their bind positions; the conversion is baked into them");
    const Fenrir::MeshData& boot = MeshOn(test, model, "Boot");
    REQUIRE(Near(boot.positions[0], Vec3(0.0f, 0.0f, 0.0f)));
    REQUIRE(Near(boot.positions[1], Vec3(0.3f, 0.0f, 0.0f)));
    REQUIRE(Near(boot.positions[2], Vec3(0.0f, 0.2f, 0.0f)));
    INFO("its joints, Foot then Knee in its skin, are remapped to the skeleton's Foot (2) and Knee (1)");
    REQUIRE(boot.skin->joints[0][0] == 2);
    REQUIRE(boot.skin->joints[2][0] == 2);
    REQUIRE(boot.skin->joints[2][1] == 1);
    const Fenrir::MeshData& shin = MeshOn(test, model, "Shin");
    REQUIRE(shin.skin->joints[0][0] == 1);
}

TEST_CASE("An incompatible skin and a joint named twice fail only their units, and the load is Partial",
          "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "incompatible-skin.gltf");
    const Fenrir::Model& model = *loaded.model;
    INFO("the load is Partial, naming GloveSkin and CloakSkin's skeleton");
    REQUIRE(loaded.result.Status() == Fenrir::LoadStatus::Partial);
    REQUIRE(loaded.result.failedUnits.size() == 2);
    REQUIRE(Contains(loaded.result.failedUnits, "GloveSkin"));
    REQUIRE(Contains(loaded.result.failedUnits, "CloakSkin"));

    INFO("SleeveSkin's skeleton stays, with its mesh");
    REQUIRE(model.skeletons.size() == 1);
    REQUIRE(EngineNames(SkeletonOf(test, model, 0)) == std::vector<std::string>{"Arm", "Hand"});
    REQUIRE(NodeNamed(model, "Sleeve").meshes.at(0).skeleton == 0);
    INFO("the failed skins' meshes are left out, never imported unskinned");
    REQUIRE(NodeNamed(model, "Glove").meshes.empty());
    REQUIRE(NodeNamed(model, "Cloak").meshes.empty());
    REQUIRE(model.meshes.size() == 2);
    INFO("the rigid mesh is untouched");
    REQUIRE(NodeNamed(model, "Rig").meshes.at(0).skeleton == -1);
    INFO("the failed skeleton's joint is an ordinary node");
    REQUIRE(NodeNamed(model, "Bone").kind == ModelNodeKind::Ordinary);

    INFO("a later load of a sub-asset of the file says the same");
    const Fenrir::LoadResult<Fenrir::Mesh> mesh =
        test.store.Load<Fenrir::Mesh>("models/incompatible-skin.gltf#Mesh0", AssetOwner::App());
    REQUIRE(mesh.Status() == Fenrir::LoadStatus::Partial);
    REQUIRE(mesh.failedUnits == loaded.result.failedUnits);
}

TEST_CASE("A non-identity owner's skinned mesh matches glTF's skinning formula", "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "owners.gltf");
    const Fenrir::Model& model = *loaded.model;
    INFO("left out: " << Joined(loaded.result.failedUnits));
    REQUIRE(loaded.result.Status() == Fenrir::LoadStatus::Succeeded);

    // owners.gltf's numbers (make_fixtures.py: OWNER_*, UPPER_REST, LOWER_REST, SLEEVE_*).
    const Mat4 ownerWorld = TRS({1.0f, 2.0f, 3.0f}, AxisAngle({1.0f, 1.0f, 0.0f}, 30.0f), {2.0f, 0.5f, 1.5f});
    const Mat4 upperLocal = TRS({0.0f, 1.0f, 0.0f}, AxisAngle({0.0f, 0.0f, 1.0f}, 20.0f));
    const Mat4 lowerLocal = TRS({0.0f, 1.0f, 0.0f}, AxisAngle({1.0f, 0.0f, 0.0f}, -35.0f));
    const Mat4 upperBind = ownerWorld * TRS({0.0f, 1.0f, 0.0f});
    const Mat4 lowerBind = upperBind * TRS({0.0f, 1.0f, 0.0f});
    const std::vector<Vec3> sleeve = {{0.1f, 1.0f, 0.0f}, {0.0f, 1.5f, 0.2f}, {-0.1f, 2.0f, 0.0f}, {0.2f, 2.5f, -0.1f}};
    const std::vector<Vec4> weights = {{0.7f, 0.3f, 0, 0}, {0.5f, 0.5f, 0, 0}, {0.6f, 0.4f, 0, 0}, {1, 0, 0, 0}};
    const std::vector<std::array<int, 2>> joints = {{0, 1}, {0, 1}, {1, 0}, {1, 0}};
    // glTF's formula: v_world = sum over joints j of weight_j x W(joint_j) x IBM_j x v_file, the node's own transform
    // ignored. The file's joint 0 is Upper and joint 1 is Lower.
    const std::array<Mat4, 2> jointWorld = {ownerWorld * upperLocal, ownerWorld * upperLocal * lowerLocal};
    const std::array<Mat4, 2> inverseBind = {glm::inverse(upperBind), glm::inverse(lowerBind)};

    const Fenrir::SkeletonData& skeleton = SkeletonOf(test, model, 0);
    REQUIRE(EngineNames(skeleton) == std::vector<std::string>{"Upper", "Lower"});
    REQUIRE(model.skeletonOwners[0] == FindNode(model, "Owner"));
    const Mat4 owner = NodeWorld(model, model.skeletonOwners[0]);
    const std::vector<Mat4> rest = RestModelMatrices(skeleton);
    const Fenrir::MeshData& mesh = MeshOn(test, model, "Sleeve");
    for (std::size_t v = 0; v < sleeve.size(); ++v)
    {
        const Vec4 file = ownerWorld * Vec4(sleeve[v], 1.0f);
        Vec3 expected(0.0f);
        for (std::size_t i = 0; i < 2; ++i)
        {
            const auto j = static_cast<std::size_t>(joints[v][i]);
            expected += weights[v][static_cast<glm::length_t>(i)] * Vec3(jointWorld[j] * inverseBind[j] * file);
        }
        INFO("vertex " << v << ": W(owner) x M_model x B x v is glTF's skinned position");
        REQUIRE(Near(Vec3(owner * Vec4(Skin(mesh, v, skeleton, rest), 1.0f)), expected, 1e-3f));
    }

    INFO("against the bind pose (Upper and Lower unrotated), M_model_bind x B is the identity; rest and bind differ");
    const std::array<Mat4, 2> bindModel = {TRS({0.0f, 1.0f, 0.0f}), TRS({0.0f, 2.0f, 0.0f})};
    for (std::size_t i = 0; i < 2; ++i)
    {
        REQUIRE(Near(bindModel[i] * skeleton.bones[i].inverseBind, Mat4(1.0f)));
        REQUIRE_FALSE(Near(rest[i] * skeleton.bones[i].inverseBind, Mat4(1.0f)));
    }
}

TEST_CASE("Baked normals and tangents follow a non-uniformly scaled owner and a mirrored one",
          "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "owners.gltf");
    const Fenrir::Model& model = *loaded.model;
    const Mat4 ownerWorld = TRS({1.0f, 2.0f, 3.0f}, AxisAngle({1.0f, 1.0f, 0.0f}, 30.0f), {2.0f, 0.5f, 1.5f});
    const glm::mat3 toBind = glm::inverse(glm::mat3(ownerWorld));
    const std::vector<Vec3> normals = {{1, 0, 0}, {0, 0, 1}, {0.6f, 0.8f, 0}, {0, 1, 0}};
    const std::vector<Vec4> tangents = {{0, 1, 0, 1}, {1, 0, 0, 1}, {0.8f, -0.6f, 0, -1}, {0, 0, 1, 1}};
    const Fenrir::MeshData& sleeve = MeshOn(test, model, "Sleeve");
    for (std::size_t v = 0; v < normals.size(); ++v)
    {
        // Any direction on the surface, moved into bind space as positions are, stays perpendicular to the normal.
        const Vec3 along = glm::cross(normals[v], Vec3(0.3f, 0.5f, 0.7f));
        const Vec3 across = glm::cross(normals[v], along);
        INFO("vertex " << v << "'s baked normal is unit length and perpendicular to its baked surface");
        REQUIRE(Near(glm::length(sleeve.normals[v]), 1.0f));
        REQUIRE(Near(glm::dot(sleeve.normals[v], glm::normalize(toBind * along)), 0.0f));
        REQUIRE(Near(glm::dot(sleeve.normals[v], glm::normalize(toBind * across)), 0.0f));
        INFO("its tangent is the file's, moved as positions are and renormalised, its handedness kept");
        REQUIRE(Near(Vec3(sleeve.tangents[v]), glm::normalize(toBind * Vec3(tangents[v]))));
        REQUIRE(sleeve.tangents[v].w == tangents[v].w);
    }

    INFO("under Mirror, S(-1, 1, 1), the tangent (-1, 0, 0) becomes (1, 0, 0), its handedness flipped to -1");
    const Fenrir::MeshData& mirrored = MeshOn(test, model, "Mirrored");
    for (std::size_t v = 0; v < 3; ++v)
    {
        REQUIRE(Near(mirrored.tangents[v], Vec4(1.0f, 0.0f, 0.0f, -1.0f)));
        REQUIRE(Near(mirrored.normals[v], Vec3(0.0f, 0.0f, 1.0f)));
    }
    REQUIRE(Near(mirrored.positions[0], Vec3(1.0f, 1.0f, 0.0f)));
}

TEST_CASE("A rotation key in the far hemisphere is flipped with its tangents", "[assets][skinned-import]")
{
    AssetsApp test;
    const Loaded loaded = Load(test, "cubic-clip.gltf");
    const Fenrir::Model& model = *loaded.model;
    REQUIRE(model.clips.size() == 1);
    const Fenrir::ClipData& clip = *test.store.TryGet(model.clips[0]);
    REQUIRE(clip.channels.size() == 1);
    const Fenrir::ClipChannel& turn = clip.channels[0];
    REQUIRE(turn.interpolation == Fenrir::ClipInterpolation::CubicSpline);
    REQUIRE(turn.playable);
    REQUIRE(turn.targetName == "Joint");

    INFO("key 0 is kept as written");
    REQUIRE(Near(turn.values[0], Vec4(0.0f, 0.0f, 0.0f, 1.0f)));
    REQUIRE(Near(turn.inTangents[0], Vec4(0.0f, 0.0f, 0.05f, 0.0f)));
    INFO("key 1's dot with key 0 is negative: its value and both tangents are negated");
    REQUIRE(Near(turn.values[1], Vec4(0.0f, 0.0f, 0.3826834f, 0.9238795f)));
    REQUIRE(Near(turn.inTangents[1], Vec4(0.0f, 0.0f, -0.1f, -0.2f)));
    REQUIRE(Near(turn.outTangents[1], Vec4(0.0f, 0.0f, 0.1f, -0.3f)));
    INFO("key 2 is near the flipped key 1, so it is kept, though it was far from key 1 as written");
    REQUIRE(Near(turn.values[2], Vec4(0.0f, 0.0f, 0.7071068f, 0.7071068f)));
    REQUIRE(Near(turn.outTangents[2], Vec4(0.0f, 0.0f, 0.02f, 0.0f)));
}

TEST_CASE("SpawnNodes leaves bones out and spawns synthetic owners", "[assets][skinned-import][spawn]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Fenrir::Model> loaded = test.store.Load<Fenrir::Model>("models/armatures.gltf", level);
    REQUIRE(loaded.Ok());
    const Fenrir::Model& model = *test.store.TryGet(loaded.handle);
    const Fenrir::SpawnedNodes spawned = test.store.SpawnNodes(level, loaded.handle);
    REQUIRE(spawned.nodes.size() == model.nodes.size());

    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        INFO("node " << model.nodes[i].name << " is spawned unless it is a bone");
        REQUIRE(spawned.nodes[i].IsValid() == (model.nodes[i].kind != ModelNodeKind::Bone));
    }
    INFO("a synthetic owner is an entity under its node, named <first root bone>.Skeleton");
    const Fenrir::Entity owner = spawned.nodes[static_cast<std::size_t>(model.skeletonOwners[0])];
    REQUIRE(std::string(owner.GetComponent<Fenrir::Name>().Get()) == "Hips.Skeleton");
    REQUIRE(owner.GetComponent<Fenrir::Relationship>().parent ==
            spawned.nodes[static_cast<std::size_t>(FindNode(model, "Scene"))]);

    INFO("an ordinary node below bones hangs from its nearest spawned ancestor, at its rest place");
    const Fenrir::Entity socket = spawned.nodes[static_cast<std::size_t>(FindNode(model, "Socket"))];
    REQUIRE(socket.GetComponent<Fenrir::Relationship>().parent ==
            spawned.nodes[static_cast<std::size_t>(FindNode(model, "Scene"))]);
    // Scene > Hips (0, 1, 0) > Spine (0, 0.5, 0) > Chest (0, 0.5, 0) > Socket (0.2, 0, 0).
    REQUIRE(Near(Vec3(Fenrir::GetWorldMatrix(socket)[3]), Vec3(0.2f, 2.0f, 0.0f)));
}
