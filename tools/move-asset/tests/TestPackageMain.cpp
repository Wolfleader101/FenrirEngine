// The smoke run's copy of the tool: built as a game builds its own, with FenrirAssets' data asset test package's
// registration added, so the fixture root's files are scanned and their references rewritten.

#include "FenrirMoveAsset/MoveAssetMain.hpp"
#include "Generated/DataAssetsTestPackageTypes.hpp"

int main(int argc, char** argv)
{
    return Fenrir::MoveAssetMain(argc, argv, {&Fenrir::Generated::RegisterDataAssetsTestPackageTypes});
}
