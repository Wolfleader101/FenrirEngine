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

**Status:** ready-for-agent

- [ ] Every FBX fixture reconstructs its bind pose within tolerance, and the axis and unit conversion lands on the root node only.
- [ ] Each FBX fixture on ADR-0031's list loads with the expected skeleton count, owners, bone kinds and attachment nodes; the incompatible and duplicate-name cases give `Partial` with only the named units failed.
- [ ] The baked clip's keys are linear at the documented rate and match the source curve within tolerance at sample times.
- [ ] Scene 2 shows the Kenney character; smoke runs still pass.
- [ ] Hand check (left to the user as a `Hand check:` line on resolve): the Kenney character stands the right way up at the right scale, skins without torn or collapsed vertices, and its clip plays with its bones drawn over the mesh.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
