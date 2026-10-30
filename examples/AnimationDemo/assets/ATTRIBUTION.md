# AnimationDemo assets

| File | What it is | Author | Licence | Source |
| --- | --- | --- | --- | --- |
| `mannequin.gltf` | A blocky humanoid on a 17-bone rig with Idle, Walk, Run, Wave and Flutter clips, one buffer embedded. | FenrirEngine, written by `make_assets.py` | MIT, as the repository | `make_assets.py` beside it |
| `critter.gltf` | A small four-legged rig with a five-bone tail and a Wag clip, one buffer embedded. | FenrirEngine, written by `make_assets.py` | MIT, as the repository | `make_assets.py` beside it |
| `quaternius/rig-human.glb` | Quaternius's human: a fully skinned mannequin, about 1.83 tall, on a 71-bone rig, with no clips of its own. | Quaternius (Tomás Laulhé), from the free Universal Animation Library packs, retargeted and repackaged by ActionForge (CGstuff) | CC0 1.0 | [ActionForge](https://actionforge.app/) (`assets/animation-library/rig/rig-human.glb`); licence in its [THIRD-PARTY-NOTICES.txt](https://actionforge.app/THIRD-PARTY-NOTICES.txt) and per clip in `assets/animation-library/manifests/library.json` |
| `quaternius/idle_loop.glb`, `walk_loop.glb`, `jog_fwd_loop.glb`, `sprint_loop.glb`, `idle_rail_call.glb` | Five of that library's clips, one file each on the same skeleton (no mesh): idle, walk, jog, sprint and a call. The walk, jog and sprint carry root motion. | Quaternius, as above | CC0 1.0 (each clip's `source.license` in the manifest) | ActionForge, `assets/animation-library/animations/<name>.glb` |
| `fox/Fox.gltf`, `fox/Fox.bin`, `fox/Texture.png` | Khronos's `Fox`, unchanged: a 24-joint fox with Survey, Walk and Run clips. | Model: PixelMannen. Rigging and animation: tomkranis. Conversion to glTF: @AsoboStudio and @scurest. | Model CC0 1.0; rigging and animation CC-BY 4.0; conversion CC-BY 4.0 (`fox/LICENSE.md`) | [glTF-Sample-Assets `Models/Fox/glTF`](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Fox) |
| `cesium-man/CesiumMan.gltf`, `cesium-man/CesiumMan_data.bin` | Khronos's `CesiumMan`, a skinned walking figure with one clip. **Changed:** its texture is left out, and its material's texture references with it, given a flat base colour instead, because the texture carries the Cesium logo, which the licence excludes. | Cesium | CC-BY 4.0 (`cesium-man/LICENSE.md`); the Cesium logo and trademark are not included | [glTF-Sample-Assets `Models/CesiumMan/glTF`](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CesiumMan) |
| `kenney/characterMedium.fbx`, `kenney/idle.fbx`, `kenney/run.fbx`, `kenney/skaterMaleA.png`, `kenney/License.txt` | Kenney's Animated Characters Protagonists 1.1, three of its files unchanged: the binary FBX character (a skinned mesh on a 58-bone rig, its foot and knee controls among them, Z up, no clips and no texture named), two of its three clip files (idle and run, each the rig with no mesh, holding a one-frame clip then the clip itself), and one of its four skins, which the demo paints on in code. `License.txt` is the pack's own. The pack's jump clip, other skins and SVG sources are left out. | Kenney (www.kenney.nl) | CC0 1.0 (`kenney/License.txt`) | [kenney.nl/assets/animated-characters-protagonists](https://kenney.nl/assets/animated-characters-protagonists) (`kenney_animated-characters-protagonists.zip`, 581,441 bytes, SHA-256 `ec3787de70fa2200256848d74201b10f6b6c3126594e9857bf989753312c2b84`, fetched 2026-10-29; the old `animated-characters-1` page redirects there) |

The mannequin and the critter are made from scratch, with no third-party content; regenerate them with
`python make_assets.py` from any directory. The procedural mannequin keeps scene 1's event-budget clip (Flutter) and
scene 3's slots; the critter is scene 3's wrong-skeleton case.

ActionForge's library has no wave clip, so the Quaternius human's fifth clip is `idle_rail_call`, a call out. More of its
85 clips (all CC0) can be fetched the same way, from `assets/animation-library/animations/<id>.glb` with the ids its
manifest lists.

Not added:

- `RobotExpressive` (Tomás Laulhé, Quaternius; CC0 1.0; three.js's `examples/models/gltf/RobotExpressive`): most of its
  body is rigid meshes on bone nodes with only the hands skinned, and meshes on bones get entities only with bone
  attachments. It suits ticket 10.
- **Mixamo characters and animations** are not checked in: Adobe's terms let them be used in a project but not shared as
  standalone files, and downloading them needs an Adobe account.
