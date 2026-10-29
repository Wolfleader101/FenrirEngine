# AnimationDemo assets

| File | What it is | Author | Licence | Source |
| --- | --- | --- | --- | --- |
| `mannequin.gltf` | A blocky humanoid on a 17-bone rig with Idle, Walk, Run, Wave and Flutter clips, one buffer embedded. | FenrirEngine, written by `make_assets.py` | MIT, as the repository | `make_assets.py` beside it |
| `critter.gltf` | A small four-legged rig with a five-bone tail and a Wag clip, one buffer embedded. | FenrirEngine, written by `make_assets.py` | MIT, as the repository | `make_assets.py` beside it |
| `fox/Fox.gltf`, `fox/Fox.bin`, `fox/Texture.png` | Khronos's `Fox`, unchanged: a 24-joint fox with Survey, Walk and Run clips. | Model: PixelMannen. Rigging and animation: tomkranis. Conversion to glTF: @AsoboStudio and @scurest. | Model CC0 1.0; rigging and animation CC-BY 4.0; conversion CC-BY 4.0 (`fox/LICENSE.md`) | [glTF-Sample-Assets `Models/Fox/glTF`](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Fox) |
| `cesium-man/CesiumMan.gltf`, `cesium-man/CesiumMan_data.bin` | Khronos's `CesiumMan`, a skinned walking figure with one clip. **Changed:** its texture is left out, and its material's texture references with it, given a flat base colour instead, because the texture carries the Cesium logo, which the licence excludes. | Cesium | CC-BY 4.0 (`cesium-man/LICENSE.md`); the Cesium logo and trademark are not included | [glTF-Sample-Assets `Models/CesiumMan/glTF`](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CesiumMan) |

The mannequin and the critter are made from scratch, with no third-party content; regenerate them with
`python make_assets.py` from any directory. The mannequin stays the humanoid of scenes 1 and 3: it has the idle, walk,
run and wave clips and the short event-budget clip those scenes need.

Not added yet:

- **A Quaternius CC0 humanoid with its own idle, walk, run and wave clips, fully skinned.** Quaternius's packs on
  quaternius.com and itch.io are downloaded by hand. `RobotExpressive` (Tomás Laulhé, Quaternius; CC0 1.0; in three.js's
  `examples/models/gltf/RobotExpressive`) has the clips, but most of its body is rigid meshes on bone nodes with only the
  hands skinned, and meshes on bones get entities only with bone attachments (ticket 10). It suits that ticket.
- **A Kenney CC0 animated FBX character:** ticket 06's.
- **Mixamo characters and animations** are not checked in: Adobe's terms let them be used in a project but not shared as
  standalone files, and downloading them needs an Adobe account.
