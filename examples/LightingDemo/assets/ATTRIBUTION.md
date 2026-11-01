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
| `moon_rock_02/`, `moon_rock_03/`, `moon_rock_07/` (each a glTF, its bin and three 1k JPEG textures: diffuse, normal (GL) and AO-roughness-metal) | Poly Haven's Moon Rock 02, 03 and 07 at 1k, unchanged: photoscanned rocks, the asteroid ring's three kinds. The bins are Poly Haven's single copy, which every resolution's glTF names. | Rico Cilliers, Greg Zaal (photography), Jenelle van Heerden (processing) | CC0 1.0 | [polyhaven.com/a/moon_rock_02](https://polyhaven.com/a/moon_rock_02), [03](https://polyhaven.com/a/moon_rock_03), [07](https://polyhaven.com/a/moon_rock_07) |
| `planets/2k_jupiter.jpg` | Solar System Scope's Jupiter texture at 2k (2048 by 1024, JPEG), unchanged: the asteroid ring's planet. | Solar System Scope (INOVE) | CC-BY 4.0, which asks for credit: "Solar System Scope", as this row gives | [solarsystemscope.com/textures](https://www.solarsystemscope.com/textures/) |
| `fir_sapling/`, `pine_sapling_small/` (each a glTF, its bin and six 1k JPEG textures: diffuse, normal (GL) and AO-roughness-metal for bark and twigs) | Poly Haven's Fir Sapling and Pine Sapling Small at 1k, unchanged: three scanned saplings each, the forest clearing's trees. | Rob Tuytel, Rico Cilliers | CC0 1.0 | [polyhaven.com/a/fir_sapling](https://polyhaven.com/a/fir_sapling), [pine_sapling_small](https://polyhaven.com/a/pine_sapling_small) |
| `fern_02/`, `shrub_03/`, `shrub_04/` (each a glTF, its bin and three 1k JPEG textures) | Poly Haven's Fern 02, Shrub 03 and Shrub 04 at 1k, unchanged: alpha-masked leaf cards, the forest clearing's undergrowth. | Rob Tuytel, Rico Cilliers (Fern 02); Rico Cilliers (the shrubs) | CC0 1.0 | [polyhaven.com/a/fern_02](https://polyhaven.com/a/fern_02), [shrub_03](https://polyhaven.com/a/shrub_03), [shrub_04](https://polyhaven.com/a/shrub_04) |
| `rock_07/`, `rock_09/` (each a glTF, its bin and three 1k JPEG textures) | Poly Haven's Rock 07 and Rock 09 at 1k, unchanged: the forest clearing's rocks and pebbles. | Jenelle van Heerden | CC0 1.0 | [polyhaven.com/a/rock_07](https://polyhaven.com/a/rock_07), [rock_09](https://polyhaven.com/a/rock_09) |
| `dead_tree_trunk/`, `tree_stump_01/`, `dry_branches_medium_01/` (each a glTF, its bin and three 1k JPEG textures) | Poly Haven's Dead Tree Trunk, Tree Stump 01 and Dry Branches Medium 01 at 1k, unchanged: the forest clearing's fallen wood. | Rob Tuytel (trunk, stump); Rico Cilliers (branches) | CC0 1.0 | [polyhaven.com/a/dead_tree_trunk](https://polyhaven.com/a/dead_tree_trunk), [tree_stump_01](https://polyhaven.com/a/tree_stump_01), [dry_branches_medium_01](https://polyhaven.com/a/dry_branches_medium_01) |
| `forest_ground_04/` (three 2k JPEG textures: diffuse, normal (GL) and AO-roughness-metal) | Poly Haven's Forest Ground 04 at 2k, unchanged: the forest clearing's floor. | Rob Tuytel, Rico Cilliers | CC0 1.0 | [polyhaven.com/a/forest_ground_04](https://polyhaven.com/a/forest_ground_04) |
| `panoramas/autumn_forest_04_2k.hdr` | Poly Haven's Autumn Forest 04 at 2k: a low sun through an autumn forest, the forest clearing's sky. | Alexander Scholten | CC0 1.0 | [polyhaven.com/a/autumn_forest_04](https://polyhaven.com/a/autumn_forest_04) |
| `panoramas/venice_sunset_2k.hdr` | Poly Haven's Venice Sunset at 2k: a clear sunset over a Venetian street and the water. | Greg Zaal | CC0 1.0 | [polyhaven.com/a/venice_sunset](https://polyhaven.com/a/venice_sunset) |
| `panoramas/studio_small_09_2k.hdr` | Poly Haven's Studio Small 09 at 2k: a small studio with softboxes. | Sergej Majboroda | CC0 1.0 | [polyhaven.com/a/studio_small_09](https://polyhaven.com/a/studio_small_09) |
| `panoramas/symmetrical_garden_02_2k.hdr` | Poly Haven's Symmetrical Garden 02 at 2k: an outdoor garden by day. | Dimitrios Savva (photography), Jarod Guest (processing) | CC0 1.0 | [polyhaven.com/a/symmetrical_garden_02](https://polyhaven.com/a/symmetrical_garden_02) |

The Poly Haven files' MD5s matched Poly Haven's API (`api.polyhaven.com/files/<id>`) when they were pinned. The moon
rocks and the Jupiter texture were checked and pinned on 2026-11-01, and the forest clearing's files (each MD5
matching Poly Haven's API, the 1k glTF packages naming their bins and textures by these paths) on the same day.
