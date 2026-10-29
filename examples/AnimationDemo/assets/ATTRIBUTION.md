# AnimationDemo assets

| File | What it is | Author | Licence | Source |
| --- | --- | --- | --- | --- |
| `mannequin.gltf` | A blocky humanoid on a 17-bone rig with Idle, Walk, Run, Wave and Flutter clips, one buffer embedded. | FenrirEngine, written by `make_assets.py` | MIT, as the repository | `make_assets.py` beside it |
| `critter.gltf` | A small four-legged rig with a five-bone tail and a Wag clip, one buffer embedded. | FenrirEngine, written by `make_assets.py` | MIT, as the repository | `make_assets.py` beside it |

Both are procedural stand-ins, made from scratch, with no third-party content. The spec's assets replace them once each
file and its licence is checked:

- **A Quaternius CC0 glTF humanoid** with idle, walk, run and wave clips on its own rig. Not added yet: the only
  Quaternius humanoid found on Poly Pizza ("Animated Base Character") is listed there as CC-BY 3.0, not CC0, and the
  CC0 packs on itch.io are downloaded by hand, so neither was confirmed.
- **Khronos's `Fox`** (glTF-Sample-Assets `Models/Fox`): model CC0 by PixelMannen; rig and animation CC-BY 4.0 by
  tomkranis; conversion to glTF CC-BY 4.0 by @AsoboStudio and @scurest. Its licence is confirmed from the repository's
  README; the files are not added yet, waiting for the download to be approved.
- **A Kenney CC0 animated FBX character:** ticket 06's.

Regenerate the stand-ins with `python make_assets.py` from any directory.
