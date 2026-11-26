// fenrir-move-asset: Assets::Move from a shell, with the engine's type registrations alone (ADR-0079 §6). A game builds
// its own copy with its registration added (README.md).

#include "FenrirMoveAsset/MoveAssetMain.hpp"

int main(int argc, char** argv)
{
    return Fenrir::MoveAssetMain(argc, argv);
}
