# move-asset

Moves or renames one file under an asset root and rewrites every reference to it, and to its sub-assets, across the root's `.json5` envelopes (ADR-0076 §5, ADR-0079 §6). It is a thin command line over `Assets::Move` (`packages/FenrirAssets/include/FenrirAssets/Assets.hpp`), which holds the rules: what is refused, which strings count as references, and how a rewritten file keeps its comments and every other byte. A future editor calls the same function.

The engine builds it with FenrirAssets (`FENRIR_ENABLE_ASSETS`, on by default), as the CMake target `fenrir-move-asset`.

## Arguments

```sh
fenrir-move-asset <root> <from> <to> [--dry-run]
```

- `<root>`: the asset root, as a path on disk (relative to the working directory, or absolute).
- `<from>`: the asset path of the file to move: root-relative, with forward slashes, as files name it.
- `<to>`: its new asset path, which must name nothing yet (a rename that changes only the case is allowed).
- `--dry-run`: write and move nothing, and print the same report.
- `--help` or `-h`: print the usage, under the name the program was run by.

It prints, on standard output, one line saying what was moved and how many references were rewritten in how many files, then each changed file (`changed: <path>`, by its path after the move, the moved file's own included) and then each `.json5` file it could not scan (`unscanned: <path>: <reason>`). An unscanned file keeps any reference to the old path, so fix it by hand.

| Exit code | Meaning |
| --- | --- |
| 0 | The file moved, or the dry run found that it could |
| 1 | Refused, nothing written; the reason is on standard error (`refused: ...`), a type registration that failed among them |
| 2 | Arguments it cannot read; the usage is on standard error |
| 3 | A write failed partway; the reason is on standard error (`failed: ...`), and standard output lists the files already rewritten |

## A game's own copy

`fenrir-move-asset` registers the engine's packages alone, as `App` and `Assets::Install` do (FenrirMath, FenrirECS, FenrirApp and FenrirAssets). The engine describes no data asset types, so to it every file of a game's types is unscanned. A game builds its own copy that adds its type registration, the one `tools/generate-type-registry` writes for it; a registration runs those it `uses` first, so the engine packages its types hold come with it.

The tool is the `FenrirMoveAsset` library behind `Fenrir::MoveAssetMain` (`include/FenrirMoveAsset/MoveAssetMain.hpp`). The game's copy is a `main` that passes its registration:

```cpp
#include "FenrirMoveAsset/MoveAssetMain.hpp"
#include "MyGame/Generated/MyGameTypes.hpp"

int main(int argc, char** argv)
{
    return Fenrir::MoveAssetMain(argc, argv, {&MyGame::Generated::RegisterMyGameTypes});
}
```

and an executable that links `FenrirMoveAsset` and the target that compiles its registration:

```cmake
add_executable(mygame-move-asset tools/MoveAsset.cpp)
target_link_libraries(mygame-move-asset PRIVATE FenrirMoveAsset MyGame)
```

## Smoke run

With `FENRIR_BUILD_TESTS` on, the CTest test `MoveAssetSmoke` runs `tests/SmokeRun.cmake`. It copies `tests/root` into the build tree and runs `fenrir-move-asset-test-package`, a copy built as above with FenrirAssets' data asset test package's registration (`tests/TestPackageMain.cpp`): a dry run that lists the changes and writes nothing, then the real move, checking the report and each rewritten file byte for byte. On the same copy it checks that a move onto an existing file is refused with exit code 1, that `fenrir-move-asset` itself lists every fixture as unscanned, and that two arguments exit 2 with the usage.
