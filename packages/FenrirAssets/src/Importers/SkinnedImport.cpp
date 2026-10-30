// The format-independent rules of skinned import (ADR-0031, ADR-0035): bones, skeletons, owners, one bind space,
// clip targets and the smallest failing unit. Each backend reads its file into a ModelImport; this runs on that, so
// glTF and FBX share it.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "FenrirAssets/AssetStore.hpp"
#include "Importers/ModelImporter.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // Two inverse binds agree, or one conversion holds, when every element matches to this, relative to its size.
        constexpr float BindTolerance = 1e-4f;
        // An owner's rest world this close to the identity keeps the backend's binds and vertices unchanged.
        constexpr float IdentityTolerance = 1e-6f;
        // A matrix with a determinant this small cannot be inverted.
        constexpr float DegenerateDeterminant = 1e-12f;
        // A synthetic owner's identity: its skeleton's first root bone's, with the top bit set, so that it never
        // repeats a node of the file.
        constexpr std::uint64_t SyntheticOwnerBit = std::uint64_t{1} << 63;

        Math::Mat4 LocalMatrix(const Transform& transform)
        {
            return glm::translate(Math::Mat4(1.0f), transform.pos) * glm::mat4_cast(transform.rot) *
                   glm::scale(Math::Mat4(1.0f), transform.scale);
        }

        bool Close(const Math::Mat4& a, const Math::Mat4& b, float tolerance)
        {
            for (glm::length_t c = 0; c < 4; ++c)
            {
                for (glm::length_t r = 0; r < 4; ++r)
                {
                    const float scale = std::max({1.0f, std::abs(a[c][r]), std::abs(b[c][r])});
                    if (!(std::abs(a[c][r] - b[c][r]) <= tolerance * scale))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        bool Invertible(const Math::Mat4& matrix)
        {
            const float determinant = glm::determinant(matrix);
            return std::isfinite(determinant) && std::abs(determinant) > DegenerateDeterminant;
        }

        bool Finite(const Math::Vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        Transform TransformOf(const Math::Mat4& matrix)
        {
            Transform transform;
            Math::Decompose(matrix, transform.pos, transform.rot, transform.scale);
            return transform;
        }

        // One skeleton while it is being built: its group of skins, then its bones and owner.
        struct Group
        {
            std::vector<std::size_t> skins;
            // The merged inverse binds by joint node, in the backend's space.
            std::map<std::uint32_t, Math::Mat4> binds;
            // Its bones' nodes, parents first.
            std::vector<std::uint32_t> bones;
            // The owner node, or -1 for none: the skeleton then gets a synthetic owner at the top of the model.
            std::int32_t owner = -1;
            std::optional<SkeletonData> data;
            Math::Mat4 ownerWorld = Math::Mat4(1.0f);
            bool ownerIsIdentity = true;
        };

        class Resolver
        {
          public:
            explicit Resolver(ModelImport& model)
                : m_model(model), m_skinFailed(model.skins.size(), false),
                  m_conversion(model.skins.size(), Math::Mat4(1.0f)), m_skeletonOf(model.nodes.size(), -1)
            {
                m_world.reserve(model.nodes.size());
                for (const ImportedNode& node : model.nodes)
                {
                    const Math::Mat4 local = LocalMatrix(node.rest);
                    m_world.push_back(node.parent >= 0 ? m_world[static_cast<std::size_t>(node.parent)] * local
                                                       : local);
                }
            }

            void Run()
            {
                MergeSkins();
                FindBones();
                for (Group& group : m_groups)
                {
                    FindOwner(group);
                    BuildSkeleton(group);
                }
                CheckMeshes();
                Emit();
                NameClipTargets();
                WarnDegenerateNodes();
            }

          private:
            const std::string& SkinName(std::size_t skin) const
            {
                return m_model.skins[skin].name;
            }

            const std::string& NodeName(std::uint32_t node) const
            {
                return m_model.nodes[node].name;
            }

            void FailSkin(std::size_t skin, std::string why)
            {
                if (!m_skinFailed[skin])
                {
                    m_skinFailed[skin] = true;
                    m_model.failures.push_back(std::move(why));
                }
            }

            // The top of the chain of format-marked bones a joint hangs from; the joint itself when it is unmarked.
            std::uint32_t MarkedRoot(std::uint32_t joint) const
            {
                std::uint32_t top = joint;
                while (m_model.nodes[top].markedBone && m_model.nodes[top].parent >= 0 &&
                       m_model.nodes[static_cast<std::size_t>(m_model.nodes[top].parent)].markedBone)
                {
                    top = static_cast<std::uint32_t>(m_model.nodes[top].parent);
                }
                return top;
            }

            // Skins grouped by the joints they share, or by the tree of marked bones their joints are in (an FBX
            // armature, whose meshes each have a skin of their own), each group in skin order, the groups by their
            // first skin.
            std::vector<Group> GroupSkins() const
            {
                std::vector<std::size_t> root(m_model.skins.size());
                std::iota(root.begin(), root.end(), std::size_t{0});
                const auto find = [&](std::size_t s) {
                    while (root[s] != s)
                    {
                        s = root[s] = root[root[s]];
                    }
                    return s;
                };
                std::map<std::uint32_t, std::size_t> firstSkin;
                std::map<std::uint32_t, std::size_t> firstInTree;
                const auto join = [&](std::map<std::uint32_t, std::size_t>& first, std::uint32_t key, std::size_t s) {
                    const auto [found, added] = first.emplace(key, s);
                    if (!added)
                    {
                        const std::size_t a = find(found->second);
                        const std::size_t b = find(s);
                        root[std::max(a, b)] = std::min(a, b);
                    }
                };
                for (std::size_t s = 0; s < m_model.skins.size(); ++s)
                {
                    if (m_skinFailed[s])
                    {
                        continue;
                    }
                    for (const std::uint32_t joint : m_model.skins[s].joints)
                    {
                        join(firstSkin, joint, s);
                        if (m_model.nodes[joint].markedBone)
                        {
                            join(firstInTree, MarkedRoot(joint), s);
                        }
                    }
                }
                std::vector<Group> groups;
                std::map<std::size_t, std::size_t> groupOf;
                for (std::size_t s = 0; s < m_model.skins.size(); ++s)
                {
                    if (m_skinFailed[s])
                    {
                        continue;
                    }
                    const auto [found, added] = groupOf.emplace(find(s), groups.size());
                    if (added)
                    {
                        groups.emplace_back();
                    }
                    groups[found->second].skins.push_back(s);
                }
                return groups;
            }

            // A joint named twice fails the whole skeleton; a later skin whose binds no one conversion takes into the
            // merged ones fails alone. Either changes the groups, so they are made again until nothing fails.
            void MergeSkins()
            {
                bool changed = true;
                while (changed)
                {
                    changed = false;
                    m_groups = GroupSkins();
                    for (Group& group : m_groups)
                    {
                        if (FailRepeatedJoints(group) || !MergeGroup(group))
                        {
                            changed = true;
                            break;
                        }
                    }
                }
            }

            bool FailRepeatedJoints(const Group& group)
            {
                for (const std::size_t s : group.skins)
                {
                    std::vector<std::uint32_t> joints = m_model.skins[s].joints;
                    std::sort(joints.begin(), joints.end());
                    const auto repeated = std::adjacent_find(joints.begin(), joints.end());
                    if (repeated == joints.end())
                    {
                        continue;
                    }
                    const std::string why = std::format("the skeleton of skin '{}' fails with its skins and meshes: "
                                                        "skin '{}' names node '{}' as a joint twice",
                                                        SkinName(group.skins[0]), SkinName(s), NodeName(*repeated));
                    m_model.failures.push_back(why);
                    for (const std::size_t failed : group.skins)
                    {
                        m_skinFailed[failed] = true;
                    }
                    return true;
                }
                return false;
            }

            // False when a skin failed.
            bool MergeGroup(Group& group)
            {
                std::vector<std::size_t> pending = group.skins;
                const ImportedSkin& first = m_model.skins[pending.front()];
                for (std::size_t j = 0; j < first.joints.size(); ++j)
                {
                    group.binds.emplace(first.joints[j], first.inverseBinds[j]);
                }
                m_conversion[pending.front()] = Math::Mat4(1.0f);
                pending.erase(pending.begin());
                while (!pending.empty())
                {
                    // The first skin left that shares a joint with those merged, else the first left: a group joined
                    // only through a tree of marked bones shares no joint, and its binds are all in one space.
                    auto next = std::find_if(pending.begin(), pending.end(), [&](std::size_t s) {
                        const std::vector<std::uint32_t>& joints = m_model.skins[s].joints;
                        return std::any_of(joints.begin(), joints.end(),
                                           [&](std::uint32_t joint) { return group.binds.contains(joint); });
                    });
                    if (next == pending.end())
                    {
                        next = pending.begin();
                    }
                    const std::size_t s = *next;
                    pending.erase(next);
                    if (!MergeSkin(group, s))
                    {
                        return false;
                    }
                }
                return true;
            }

            // Binds agree, or one conversion C takes the later skin's vertex space into the merged one's:
            // B_merged[j] x C = B_later[j] for every shared joint (ADR-0031).
            bool MergeSkin(Group& group, std::size_t s)
            {
                const ImportedSkin& skin = m_model.skins[s];
                std::vector<std::size_t> shared;
                for (std::size_t j = 0; j < skin.joints.size(); ++j)
                {
                    if (group.binds.contains(skin.joints[j]))
                    {
                        shared.push_back(j);
                    }
                }
                const auto agrees = [&](const Math::Mat4& conversion) {
                    return std::all_of(shared.begin(), shared.end(), [&](std::size_t j) {
                        return Close(group.binds.at(skin.joints[j]) * conversion, skin.inverseBinds[j], BindTolerance);
                    });
                };
                Math::Mat4 conversion(1.0f);
                if (!shared.empty() && !agrees(conversion))
                {
                    const Math::Mat4& merged = group.binds.at(skin.joints[shared.front()]);
                    if (Invertible(merged))
                    {
                        conversion = glm::inverse(merged) * skin.inverseBinds[shared.front()];
                    }
                    if (!Invertible(merged) || !Invertible(conversion) || !agrees(conversion))
                    {
                        FailSkin(s, std::format("skin '{}' fails with its meshes: it shares joints with skin '{}', "
                                                "but no one conversion takes its binds into that skin's",
                                                skin.name, SkinName(group.skins.front())));
                        return false;
                    }
                }
                m_conversion[s] = conversion;
                const Math::Mat4 undo = glm::inverse(conversion);
                for (std::size_t j = 0; j < skin.joints.size(); ++j)
                {
                    group.binds.emplace(skin.joints[j], skin.inverseBinds[j] * undo);
                }
                return true;
            }

            // Joints, the nodes on the path from a joint up to another joint, and nodes below a bone that a clip
            // animates and that carry no mesh, camera or light (ADR-0031's bone rules for glTF).
            void FindBones()
            {
                for (std::size_t k = 0; k < m_groups.size(); ++k)
                {
                    const auto skeleton = static_cast<std::int32_t>(k);
                    for (const auto& [joint, bind] : m_groups[k].binds)
                    {
                        m_skeletonOf[joint] = skeleton;
                    }
                }
                for (std::size_t k = 0; k < m_groups.size(); ++k)
                {
                    const auto skeleton = static_cast<std::int32_t>(k);
                    for (const auto& [joint, bind] : m_groups[k].binds)
                    {
                        std::vector<std::uint32_t> path;
                        std::int32_t above = m_model.nodes[joint].parent;
                        while (above >= 0 && !m_groups[k].binds.contains(static_cast<std::uint32_t>(above)))
                        {
                            path.push_back(static_cast<std::uint32_t>(above));
                            above = m_model.nodes[static_cast<std::size_t>(above)].parent;
                        }
                        if (above >= 0)
                        {
                            for (const std::uint32_t node : path)
                            {
                                m_skeletonOf[node] = skeleton;
                            }
                        }
                    }
                }
                FindMarkedBones();
                std::vector<bool> animated(m_model.nodes.size(), false);
                for (const ImportedClip& clip : m_model.clips)
                {
                    for (const std::uint32_t target : clip.targets)
                    {
                        animated[target] = true;
                    }
                }
                for (std::size_t i = 0; i < m_model.nodes.size(); ++i)
                {
                    const ImportedNode& node = m_model.nodes[i];
                    if (m_skeletonOf[i] < 0 && node.parent >= 0 && animated[i] && !node.carriesContent)
                    {
                        m_skeletonOf[i] = m_skeletonOf[static_cast<std::size_t>(node.parent)];
                    }
                }
                for (std::size_t k = 0; k < m_groups.size(); ++k)
                {
                    OrderBones(m_groups[k], static_cast<std::int32_t>(k));
                }
            }

            // Nodes the format marks as bones (ADR-0031's fourth rule, FBX's LimbNode): a chain of them above a
            // skeleton's root bones joins it, as does one below a bone. Marked nodes no skin reaches (an FBX file of
            // clips, its armature without a mesh) make a skeleton of their own, one per node their trees hang from,
            // bound at their rest; a tree with a failed skin's joint stays ordinary nodes, as the skeleton failed.
            void FindMarkedBones()
            {
                std::vector<bool> failedTree(m_model.nodes.size(), false);
                for (std::size_t s = 0; s < m_model.skins.size(); ++s)
                {
                    for (const std::uint32_t joint : m_model.skins[s].joints)
                    {
                        if (m_skinFailed[s] && m_model.nodes[joint].markedBone)
                        {
                            failedTree[MarkedRoot(joint)] = true;
                        }
                    }
                }
                for (std::size_t i = m_model.nodes.size(); i-- > 0;)
                {
                    const std::int32_t skeleton = m_skeletonOf[i];
                    if (skeleton < 0)
                    {
                        continue;
                    }
                    for (std::int32_t above = m_model.nodes[i].parent;
                         above >= 0 && m_skeletonOf[static_cast<std::size_t>(above)] < 0 &&
                         m_model.nodes[static_cast<std::size_t>(above)].markedBone;
                         above = m_model.nodes[static_cast<std::size_t>(above)].parent)
                    {
                        m_skeletonOf[static_cast<std::size_t>(above)] = skeleton;
                    }
                }
                // Each skeleton by the node its root bones hang from: an unreached tree of marked bones beside them
                // (a rig's controls no skin names) joins it, and trees with no skeleton beside them share one.
                std::map<std::int32_t, std::int32_t> byParent;
                for (std::size_t i = 0; i < m_model.nodes.size(); ++i)
                {
                    const std::int32_t parent = m_model.nodes[i].parent;
                    if (m_skeletonOf[i] >= 0 && (parent < 0 || m_skeletonOf[static_cast<std::size_t>(parent)] < 0))
                    {
                        byParent.emplace(parent, m_skeletonOf[i]);
                    }
                }
                for (std::size_t i = 0; i < m_model.nodes.size(); ++i)
                {
                    const ImportedNode& node = m_model.nodes[i];
                    if (!node.markedBone || m_skeletonOf[i] >= 0)
                    {
                        continue;
                    }
                    if (node.parent >= 0 && m_skeletonOf[static_cast<std::size_t>(node.parent)] >= 0)
                    {
                        m_skeletonOf[i] = m_skeletonOf[static_cast<std::size_t>(node.parent)];
                        continue;
                    }
                    if (failedTree[i] || (node.parent >= 0 && failedTree[static_cast<std::size_t>(node.parent)]))
                    {
                        failedTree[i] = true;
                        continue;
                    }
                    const auto [found, added] =
                        byParent.emplace(node.parent, static_cast<std::int32_t>(m_groups.size()));
                    if (added)
                    {
                        m_groups.emplace_back();
                    }
                    m_skeletonOf[i] = found->second;
                }
            }

            // Skin order, then joint order, then parents first; bones that are no joint come with the first bone
            // below them, or after the joints in node order.
            void OrderBones(Group& group, std::int32_t skeleton)
            {
                std::vector<bool> placed(m_model.nodes.size(), false);
                const auto place = [&](auto& self, std::uint32_t node) -> void {
                    if (placed[node])
                    {
                        return;
                    }
                    placed[node] = true;
                    const std::int32_t parent = m_model.nodes[node].parent;
                    if (parent >= 0 && m_skeletonOf[static_cast<std::size_t>(parent)] == skeleton)
                    {
                        self(self, static_cast<std::uint32_t>(parent));
                    }
                    group.bones.push_back(node);
                };
                for (const std::size_t s : group.skins)
                {
                    for (const std::uint32_t joint : m_model.skins[s].joints)
                    {
                        place(place, joint);
                    }
                }
                for (std::size_t i = 0; i < m_model.nodes.size(); ++i)
                {
                    if (m_skeletonOf[i] == skeleton)
                    {
                        place(place, static_cast<std::uint32_t>(i));
                    }
                }
            }

            bool IsBone(std::int32_t node) const
            {
                return node >= 0 && m_skeletonOf[static_cast<std::size_t>(node)] >= 0;
            }

            bool IsAbove(std::int32_t ancestor, std::uint32_t node) const
            {
                for (std::int32_t above = m_model.nodes[node].parent; above >= 0;
                     above = m_model.nodes[static_cast<std::size_t>(above)].parent)
                {
                    if (above == ancestor)
                    {
                        return true;
                    }
                }
                return false;
            }

            // The skin.skeleton node when it is no bone and above every root bone, else the nearest non-bone
            // ancestor of all of them. skin.skeleton names the owner only; it never enters the skinning maths.
            void FindOwner(Group& group)
            {
                std::vector<std::uint32_t> roots;
                for (const std::uint32_t bone : group.bones)
                {
                    const std::int32_t parent = m_model.nodes[bone].parent;
                    if (parent < 0 || m_skeletonOf[static_cast<std::size_t>(parent)] != m_skeletonOf[bone])
                    {
                        roots.push_back(bone);
                    }
                }
                const auto aboveAll = [&](std::int32_t node) {
                    return std::all_of(roots.begin(), roots.end(),
                                       [&](std::uint32_t root) { return IsAbove(node, root); });
                };
                for (const std::size_t s : group.skins)
                {
                    const std::int32_t named = m_model.skins[s].skeletonNode;
                    if (named < 0 || IsBone(named))
                    {
                        continue;
                    }
                    if (aboveAll(named))
                    {
                        group.owner = named;
                        return;
                    }
                    m_model.warnings.push_back(std::format("skin '{}' names node '{}' as its skeleton, which is not "
                                                           "above its root bones; its owner is found from its bones",
                                                           SkinName(s), NodeName(static_cast<std::uint32_t>(named))));
                    break;
                }
                for (std::int32_t above = m_model.nodes[roots.front()].parent; above >= 0;
                     above = m_model.nodes[static_cast<std::size_t>(above)].parent)
                {
                    if (!IsBone(above) && aboveAll(above))
                    {
                        group.owner = above;
                        return;
                    }
                }
                group.owner = -1;
            }

            std::string SkeletonName(const Group& group) const
            {
                return NodeName(group.bones.front());
            }

            void FailSkeleton(Group& group, const std::string& why)
            {
                m_model.failures.push_back(
                    std::format("skeleton '{}' fails with its skins and meshes: {}", SkeletonName(group), why));
                for (const std::size_t s : group.skins)
                {
                    m_skinFailed[s] = true;
                }
                group.data.reset();
            }

            // Into the owner's bind space: B = B_backend x W_o, the vertices through W_o^-1, unchanged when W_o is the
            // identity. A bone that is no joint has the bind its parent's bind and its own rest transform give it.
            void BuildSkeleton(Group& group)
            {
                if (group.owner >= 0)
                {
                    group.ownerWorld = m_world[static_cast<std::size_t>(group.owner)];
                }
                if (!Invertible(group.ownerWorld))
                {
                    FailSkeleton(group, std::format("its owner '{}' has a rest world that cannot be inverted",
                                                    NodeName(static_cast<std::uint32_t>(group.owner))));
                    return;
                }
                group.ownerIsIdentity = Close(group.ownerWorld, Math::Mat4(1.0f), IdentityTolerance);
                const Math::Mat4 ownerInverse = glm::inverse(group.ownerWorld);

                SkeletonData data;
                std::map<std::uint32_t, std::int32_t> boneOf;
                std::vector<Math::Mat4> bindModel;
                for (const std::uint32_t node : group.bones)
                {
                    const ImportedNode& source = m_model.nodes[node];
                    SkeletonBone bone;
                    bone.sourceName = source.name;
                    bone.sourceId = source.sourceId;
                    bone.modelNode = static_cast<std::int32_t>(node);
                    const auto parent = boneOf.find(static_cast<std::uint32_t>(source.parent));
                    bone.parent = source.parent >= 0 && parent != boneOf.end() ? parent->second : NoBone;
                    if (bone.parent != NoBone || source.parent == group.owner)
                    {
                        bone.rest = source.rest;
                    }
                    else
                    {
                        // A root bone further below its owner: its rest relative to the owner.
                        bone.rest = TransformOf(ownerInverse * m_world[node]);
                    }
                    if (const auto bind = group.binds.find(node); bind != group.binds.end())
                    {
                        if (!Invertible(bind->second))
                        {
                            FailSkeleton(group, std::format("joint '{}' has an inverse bind that cannot be inverted",
                                                            source.name));
                            return;
                        }
                        bone.inverseBind = group.ownerIsIdentity ? bind->second : bind->second * group.ownerWorld;
                        bindModel.push_back(glm::inverse(bone.inverseBind));
                    }
                    else
                    {
                        // A root bone that is no joint (a marked bone above the joints) is bound at its rest.
                        const Math::Mat4 model =
                            bone.parent != NoBone
                                ? bindModel[static_cast<std::size_t>(bone.parent)] * LocalMatrix(source.rest)
                                : LocalMatrix(bone.rest);
                        if (!Invertible(model))
                        {
                            FailSkeleton(group,
                                         std::format("bone '{}' has a bind that cannot be inverted", source.name));
                            return;
                        }
                        bindModel.push_back(model);
                        bone.inverseBind = glm::inverse(model);
                    }
                    boneOf.emplace(node, static_cast<std::int32_t>(data.bones.size()));
                    data.bones.push_back(std::move(bone));
                }
                AssetWarnings warnings;
                if (const std::string problem = PrepareAsset(data, warnings); !problem.empty())
                {
                    FailSkeleton(group, problem);
                    return;
                }
                for (const std::string& warning : warnings)
                {
                    m_model.warnings.push_back(std::format("skeleton '{}': {}", SkeletonName(group), warning));
                }
                group.data = std::move(data);
                for (const auto& [node, bone] : boneOf)
                {
                    m_boneOf[node] = bone;
                }
            }

            std::int32_t GroupOfSkin(std::size_t skin) const
            {
                for (std::size_t k = 0; k < m_groups.size(); ++k)
                {
                    const std::vector<std::size_t>& skins = m_groups[k].skins;
                    if (std::find(skins.begin(), skins.end(), skin) != skins.end())
                    {
                        return static_cast<std::int32_t>(k);
                    }
                }
                return -1;
            }

            // A weight that is negative or not finite, or a weighted joint index past the skin's joints, fails the
            // skin with every mesh drawn with it (ADR-0035).
            void CheckMeshes()
            {
                for (const ImportedMesh& mesh : m_model.meshes)
                {
                    if (mesh.skin < 0 || m_skinFailed[static_cast<std::size_t>(mesh.skin)])
                    {
                        continue;
                    }
                    const auto skin = static_cast<std::size_t>(mesh.skin);
                    const std::size_t joints = m_model.skins[skin].joints.size();
                    for (std::size_t v = 0; v < mesh.weights.size(); ++v)
                    {
                        for (glm::length_t i = 0; i < 4; ++i)
                        {
                            const float weight = mesh.weights[v][i];
                            if (!(weight >= 0.0f) || !std::isfinite(weight))
                            {
                                FailSkin(skin, std::format("skin '{}' fails with its meshes: mesh '{}' has a negative "
                                                           "or non-finite bone weight",
                                                           SkinName(skin), mesh.name));
                            }
                            else if (weight > 0.0f && mesh.joints[v][static_cast<std::size_t>(i)] >= joints)
                            {
                                FailSkin(skin, std::format("skin '{}' fails with its meshes: mesh '{}' names joint {}, "
                                                           "and the skin has {}",
                                                           SkinName(skin), mesh.name,
                                                           mesh.joints[v][static_cast<std::size_t>(i)], joints));
                            }
                        }
                    }
                }
            }

            // The surviving skeletons, numbered; synthetic owners where several share a node or none has one; node
            // kinds; and every skinned mesh baked into its skeleton's bind space.
            void Emit()
            {
                std::vector<std::int32_t> index(m_groups.size(), -1);
                std::map<std::int32_t, std::size_t> perOwner;
                for (std::size_t k = 0; k < m_groups.size(); ++k)
                {
                    if (m_groups[k].data)
                    {
                        index[k] = static_cast<std::int32_t>(m_model.skeletons.size());
                        m_model.skeletons.push_back({std::move(*m_groups[k].data), 0});
                        ++perOwner[m_groups[k].owner];
                    }
                }
                for (std::size_t k = 0; k < m_groups.size(); ++k)
                {
                    if (index[k] < 0)
                    {
                        continue;
                    }
                    Group& group = m_groups[k];
                    ImportedSkeleton& skeleton = m_model.skeletons[static_cast<std::size_t>(index[k])];
                    std::int32_t owner = group.owner;
                    if (owner < 0 || perOwner[owner] > 1)
                    {
                        const ImportedNode& first = m_model.nodes[group.bones.front()];
                        ImportedNode synthetic;
                        synthetic.name = first.name + ".Skeleton";
                        synthetic.sourceId = SyntheticOwnerBit | first.sourceId;
                        synthetic.parent = owner;
                        synthetic.kind = ModelNodeKind::SyntheticOwner;
                        owner = static_cast<std::int32_t>(m_model.nodes.size());
                        m_model.nodes.push_back(std::move(synthetic));
                    }
                    skeleton.owner = static_cast<std::uint32_t>(owner);
                    for (std::size_t b = 0; b < group.bones.size(); ++b)
                    {
                        ImportedNode& node = m_model.nodes[group.bones[b]];
                        node.kind = ModelNodeKind::Bone;
                        node.skeleton = index[k];
                        node.bone = static_cast<std::int32_t>(b);
                    }
                }

                for (ImportedMesh& mesh : m_model.meshes)
                {
                    if (mesh.skin < 0)
                    {
                        continue;
                    }
                    const auto skin = static_cast<std::size_t>(mesh.skin);
                    const std::int32_t k = GroupOfSkin(skin);
                    if (m_skinFailed[skin] || k < 0 || index[static_cast<std::size_t>(k)] < 0)
                    {
                        mesh.failed = true;
                        continue;
                    }
                    Bake(mesh, m_groups[static_cast<std::size_t>(k)], index[static_cast<std::size_t>(k)]);
                }
            }

            void Bake(ImportedMesh& mesh, const Group& group, std::int32_t skeleton)
            {
                const ImportedSkin& skin = m_model.skins[static_cast<std::size_t>(mesh.skin)];
                MeshSkin section;
                section.joints.resize(mesh.joints.size());
                section.weights.resize(mesh.weights.size());
                for (std::size_t v = 0; v < mesh.joints.size(); ++v)
                {
                    for (std::size_t i = 0; i < 4; ++i)
                    {
                        // An unweighted slot may name any joint; it is stored as bone 0.
                        const bool weighted = mesh.weights[v][static_cast<glm::length_t>(i)] > 0.0f;
                        section.joints[v][i] =
                            weighted ? static_cast<std::uint16_t>(m_boneOf.at(skin.joints[mesh.joints[v][i]])) : 0;
                    }
                    section.weights[v] = QuantiseBoneWeights(mesh.weights[v]).value_or(BoneQuad{});
                }
                const SkeletonData& data = m_model.skeletons[static_cast<std::size_t>(skeleton)].data;
                section.expectedSignature = data.BindSignature();
                mesh.data.skin = std::move(section);
                mesh.skeleton = skeleton;

                Math::Mat4 bake = m_conversion[static_cast<std::size_t>(mesh.skin)];
                if (!group.ownerIsIdentity)
                {
                    bake = glm::inverse(group.ownerWorld) * bake;
                }
                if (!Close(bake, Math::Mat4(1.0f), 0.0f))
                {
                    BakeVertices(mesh.data, bake);
                }
            }

            // Each channel names its target by identity and engine name. One on an ordinary node is kept but not
            // played; rotations are kept in one hemisphere, a key flipped with its tangents when its dot with the key
            // before is negative.
            void NameClipTargets()
            {
                for (ImportedClip& clip : m_model.clips)
                {
                    for (std::size_t c = 0; c < clip.data.channels.size(); ++c)
                    {
                        ClipChannel& channel = clip.data.channels[c];
                        const ImportedNode& target = m_model.nodes[clip.targets[c]];
                        channel.targetSourceId = target.sourceId;
                        channel.playable = target.kind == ModelNodeKind::Bone;
                        channel.targetName =
                            channel.playable
                                ? std::string(
                                      m_model.skeletons[static_cast<std::size_t>(target.skeleton)].data.EngineName(
                                          target.bone))
                                : target.name;
                        if (channel.path != ClipPath::Rotation)
                        {
                            continue;
                        }
                        for (std::size_t k = 1; k < channel.values.size(); ++k)
                        {
                            if (glm::dot(channel.values[k], channel.values[k - 1]) >= 0.0f)
                            {
                                continue;
                            }
                            channel.values[k] = -channel.values[k];
                            if (k < channel.inTangents.size())
                            {
                                channel.inTangents[k] = -channel.inTangents[k];
                            }
                            if (k < channel.outTangents.size())
                            {
                                channel.outTangents[k] = -channel.outTangents[k];
                            }
                        }
                    }
                }
            }

            // An ordinary node with a degenerate transform is kept, with a warning; a bone's fails its skeleton.
            void WarnDegenerateNodes()
            {
                for (const ImportedNode& node : m_model.nodes)
                {
                    if (node.kind == ModelNodeKind::Bone)
                    {
                        continue;
                    }
                    const Math::Vec3& scale = node.rest.scale;
                    const bool degenerate = !Finite(node.rest.pos) || !Finite(scale) ||
                                            std::min({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)}) < 1e-8f;
                    if (degenerate)
                    {
                        m_model.warnings.push_back(
                            std::format("node '{}' has a degenerate transform; it is kept as it is", node.name));
                    }
                }
            }

            ModelImport& m_model;
            std::vector<bool> m_skinFailed;
            // Each skin's conversion into its skeleton's merged binds, C, baked into its vertices.
            std::vector<Math::Mat4> m_conversion;
            // Each node's group while bones are found, -1 for an ordinary node.
            std::vector<std::int32_t> m_skeletonOf;
            std::map<std::uint32_t, std::int32_t> m_boneOf;
            std::vector<Math::Mat4> m_world;
            std::vector<Group> m_groups;
        };
    } // namespace

    void ResolveSkinning(ModelImport& model)
    {
        Resolver(model).Run();
    }

    void BakeVertices(MeshData& mesh, const Math::Mat4& matrix)
    {
        const Math::Mat3 linear(matrix);
        const Math::Mat3 normalMatrix = glm::transpose(glm::inverse(linear));
        const float handedness = glm::determinant(linear) < 0.0f ? -1.0f : 1.0f;
        for (Math::Vec3& position : mesh.positions)
        {
            position = Math::Vec3(matrix * Math::Vec4(position, 1.0f));
        }
        for (Math::Vec3& normal : mesh.normals)
        {
            const Math::Vec3 moved = normalMatrix * normal;
            const float length = glm::length(moved);
            normal = length > 0.0f ? moved / length : normal;
        }
        for (Math::Vec4& tangent : mesh.tangents)
        {
            const Math::Vec3 moved = linear * Math::Vec3(tangent);
            const float length = glm::length(moved);
            tangent = Math::Vec4(length > 0.0f ? moved / length : Math::Vec3(tangent), tangent.w * handedness);
        }
    }
} // namespace Fenrir::Detail
