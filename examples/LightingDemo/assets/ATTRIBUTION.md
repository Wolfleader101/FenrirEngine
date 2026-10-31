# LightingDemo assets

## Committed

| File | What it is | Author | Licence | Source |
| --- | --- | --- | --- | --- |
| `panoramas/brown_photostudio_02_1k.hdr` | Poly Haven's Brown Photostudio 02 at 1k (1024 by 512, Radiance HDR, 1,648,130 bytes), unchanged: a studio with large softboxes, the material grid's sky. | Sergej Majboroda | CC0 1.0 | [polyhaven.com/a/brown_photostudio_02](https://polyhaven.com/a/brown_photostudio_02) (`dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/brown_photostudio_02_1k.hdr`, SHA-256 `469abc5e6035b6c450bae8adbb448f8d1fa7c51d9e384e501516c28352c91b58`, its MD5 matching Poly Haven's API, fetched 2026-10-31) |

## Fetched

Not in the repository: `FENRIR_FETCH_DEMO_ASSETS` downloads them at configure time (ADR-0052), each pinned by URL and
SHA-256 in `../fetched-assets.json`, which also gives each file's SPDX licence. Each file and licence was checked on
2026-10-31; the Khronos models are pinned to glTF-Sample-Assets commit `8f492390ddc7a2ae221a1369ef560589cf8c70fb`, as
`examples/assets/BoxTextured` is.

| Files | What it is | Author | Licence | Source |
| --- | --- | --- | --- | --- |
| `DamagedHelmet/` (glTF, bin and five JPEG textures) | Khronos's `DamagedHelmet`, unchanged: a battle-damaged sci-fi helmet with base colour, metallic-roughness, normal, occlusion and emissive maps. | ctxwing (rebuild and conversion to glTF), after an earlier model by theblueturtle_ | CC-BY 4.0 (the rebuild) and CC-BY-NC 4.0 (the earlier model), as its `LICENSE.md` says: **non-commercial**, which is why it is fetched for viewing and never shipped | [glTF-Sample-Assets `Models/DamagedHelmet/glTF`](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet) |
| `FlightHelmet/` (glTF, bin and fifteen PNG textures) | Khronos's `FlightHelmet`, unchanged: a WWII flight helmet of leather, metal, glass, rubber and wood, in five materials. | Gary Hsu (conversion from Maya) | CC0 1.0 | [glTF-Sample-Assets `Models/FlightHelmet/glTF`](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/FlightHelmet) |
| `SciFiHelmet/` (glTF, bin and four PNG textures) | Khronos's `SciFiHelmet`, unchanged: a sci-fi helmet with base colour, metallic-roughness, normal and occlusion maps, and tangents. | Michael Pavlovic; conversion to glTF by Norbert Nopper | CC0 1.0 | [glTF-Sample-Assets `Models/SciFiHelmet/glTF`](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SciFiHelmet) |
| `panoramas/venice_sunset_2k.hdr` | Poly Haven's Venice Sunset at 2k: a clear sunset over a Venetian street and the water. | Greg Zaal | CC0 1.0 | [polyhaven.com/a/venice_sunset](https://polyhaven.com/a/venice_sunset) |
| `panoramas/studio_small_09_2k.hdr` | Poly Haven's Studio Small 09 at 2k: a small studio with softboxes. | Sergej Majboroda | CC0 1.0 | [polyhaven.com/a/studio_small_09](https://polyhaven.com/a/studio_small_09) |
| `panoramas/symmetrical_garden_02_2k.hdr` | Poly Haven's Symmetrical Garden 02 at 2k: an outdoor garden by day. | Dimitrios Savva (photography), Jarod Guest (processing) | CC0 1.0 | [polyhaven.com/a/symmetrical_garden_02](https://polyhaven.com/a/symmetrical_garden_02) |

The Poly Haven files' MD5s matched Poly Haven's API (`api.polyhaven.com/files/<id>`) when they were pinned.
