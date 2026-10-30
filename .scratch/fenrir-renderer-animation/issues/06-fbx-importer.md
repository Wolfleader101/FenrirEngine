# 06: the FBX importer reads skins, skeletons and clips into the same bind space, and the Kenney character joins the Skeleton scene

**What to build:** loading an FBX file with skinned meshes gives the same `Model` shape as glTF, through ticket 02's shared rules, and a real Kenney rig animates skinned in AnimationDemo's scene 2 (spec: "Import"; ADR-0031, ADR-0034, ADR-0035). The spec names FBX through ufbx with real rigs as a risk, which is why it has its own ticket and its own on-screen check.

- **Bones:** an FBX `LimbNode` is a bone, besides ticket 02's rules.
- **Bind space:** each skin from ufbx's `bind_to_world` and `geometry_to_bone`, satisfying `v_world = W(owner) × M_model[i] × B[i] × v` like glTF; ufbx's axis and unit conversion sits on the root node. Skeleton grouping, owners, merging, baking, validation, `Partial` results and `expectedSignature` reuse ticket 02's code.
- **Clips:** baked through ufbx's baking API to linear keys at a documented internal rate, with the rotation hemisphere flip applied as for glTF; channels on ordinary nodes kept but unplayable.
- **The ufbx importer's "skinned mesh left out" diagnostic** goes.
- **Fixtures:** the FBX half of ADR-0031's mandatory list: multiple and unrelated armatures, shared-joint skins, unweighted leaves, intermediate nodes, non-bone attachments, shared skeletons, an incompatible skin and duplicate names, plus a scaled-unit, rotated-axis file and a baked clip.
- **AnimationDemo:** one Kenney CC0 animated FBX character, its exact files and licence checked first and recorded in `ATTRIBUTION.md`, placed beside the humanoid and the Fox in scene 2 with its bones drawn, and playing one of its clips. The README's scene 2 row and hand checks gain it.

**Blocked by:** 02, 05

**Touches:** `packages/FenrirAssets/src/Importers/Ufbx/`, `packages/FenrirAssets/tests/`, `examples/AnimationDemo/assets/`, `examples/AnimationDemo/src/`, `examples/AnimationDemo/README.md`

**Status:** resolved

- [x] Every FBX fixture reconstructs its bind pose within tolerance, and the axis and unit conversion lands on the root node only.
- [x] Each FBX fixture on ADR-0031's list loads with the expected skeleton count, owners, bone kinds and attachment nodes; the incompatible and duplicate-name cases give `Partial` with only the named units failed.
- [x] The baked clip's keys are linear at the documented rate and match the source curve within tolerance at sample times.
- [x] Scene 2 shows the Kenney character; smoke runs still pass.
Hand check: in scene 2 (`FENRIR_DEMO_SCENE=Skeleton`, on OpenGL), Kenney's character, rightmost, stands the right way up at a person's size beside the humanoids and the Fox, painted with its skater skin, skins without torn or collapsed vertices, and runs in place with its yellow bone lines and axes drawn over the mesh; M, B and N hide its mesh, hide its bones and show its bone names as for the others.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-29 on `claude/animation-05-skinning-9357f4` (ticket 05, PR #241, not merged yet), as a stacked PR. Decisions to review:

- **Conversion on the root node:** ufbx now loads with `UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS` (was `MODIFY_GEOMETRY`, which scaled every node's translation and the geometry for units). The axis and unit conversion is folded into the rest transform of the file's root nodes only; nodes below and rigid vertices keep the file's units. ufbx's cluster `bind_to_world` is in its converted world, which the nodes' rest worlds compose to, so skins resolve as ADR-0031 says. A static FBX in metres and Y up (`scene.fbx`) is unchanged.
- **Skinned vertices** are read in ufbx's world at the bind (`bind_to_world x geometry_to_bone`), checked to agree across the skin's clusters (1e-4, relative), so `ResolveSkinning` takes them as it takes glTF's (`v = W_o^-1 x v`). A cluster with no bone, no clusters, or clusters that disagree fail that skin and its mesh (`Partial`). A mesh with several skin deformers uses its first, with a warning. A vertex keeps its four largest influences; one with none rides rigidly in bind space (the store's existing warning).
- **LimbNode rule (`ImportedNode::markedBone`, set from ufbx's `node.bone`, so `Limb` and `Root` skeleton nodes count too):** a chain of marked nodes above a skeleton's root bones joins it (bound at its rest), one below a bone joins its parent's skeleton, and marked trees no skin reaches make a skeleton of their own, bound at their rest, one per node their trees hang from (and joining a skinned skeleton whose root bones hang from the same node). This is what makes an FBX clip file with no mesh (Kenney's, and animation packs generally) playable: its channels target bones.
- **Skins in one tree of marked bones are one skeleton**, even when they share no joint: in FBX each mesh has its own skin deformer, so two meshes of one armature often name disjoint joints. They merge with no conversion (their binds are in one world), so ADR-0031's "disjoint skins are separate skeletons" holds for skins in separate armatures. A tree holding a failed skin's joint stays ordinary nodes, so a failed skeleton is not brought back as a skinless one.
- **Clip baking:** through `ufbx_bake_anim` with `resample_rate` and `minimum_sample_rate` 30 (`ClipSampleRate`): a stretch ufbx cannot keep linear (cubic curves, and rotations, which FBX interpolates as Euler angles) is resampled at 30 keys a second; keys the file already gives at 30 a second or faster, and the ends of linear stretches, are kept. Start time trimmed to 0; a stepped key becomes two keys 1 ms apart. Each baked node gives translation, rotation and scale channels, constant ones included, and the shared hemisphere flip runs on them. A stack that cannot be baked is a failed unit. Clip names are the stacks'.
- **Duplicate names:** read as glTF's case: a repeated bone name gets a suffixed engine name and the load succeeds (`armatures.fbx`'s two Pelvis bones); the `Partial` cases are the incompatible skin and a bone named twice in one skin (`incompatible-skin.fbx`), as in the glTF fixtures.
- **Fixtures:** `armatures.fbx`, `shared-skins.fbx`, `incompatible-skin.fbx`, `units.fbx` (Z up, centimetres), `baked-clip.fbx` and `clips-only.fbx`, ASCII FBX 7.4 from `make_fixtures.py`'s new `Fbx` writer (no FBX SDK; its float format is now `%.12g`, which leaves every existing fixture byte-identical). The tests are in `Tests/SkinnedImport.cpp`, tagged `[fbx]`.
- **Kenney:** "Animated Characters Protagonists" 1.1 (the `animated-characters-1` page redirects there), CC0, downloaded with the user's approval through the site's own download button; checked in: the character FBX, the idle and run clip files, the `skaterMaleA` skin and the pack's `License.txt` (`assets/kenney/`, recorded in `ATTRIBUTION.md` with the zip's SHA-256). The file names no texture, so the demo paints the skin on (`Paint` in `Rigs.cpp`); each clip file holds a one-frame stack before the clip, so `LoadClip` takes an index. Scene 2's row is spread a little wider and the camera moved back to fit five.
- **ADR-0031 refined** with the FBX grouping rule above and the 30-a-second bake rate (the spec's "documented internal rate"), so the ADR stays true.
