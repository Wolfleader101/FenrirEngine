#pragma once

#include <initializer_list>

namespace Fenrir
{
    class TypeRegistry;

    /** @brief A package's type registration, such as a game's generated `Register<Package>Types`. */
    using TypeRegistration = bool (*)(TypeRegistry& registry);

    /**
     * @brief The whole of `move-asset <root> <from> <to> [--dry-run]` (ADR-0079 §6): makes a type registry, registers
     * the engine's packages in it as App and Assets::Install do (FenrirMath, FenrirECS, FenrirApp, FenrirAssets) and
     * then each of `registrations`, calls Assets::Move, and prints its MoveResult.
     *
     * On standard output: one line saying what was moved (or, on a dry run, what would be), then each changed file as
     * `changed: <path>`, then each unscanned one as `unscanned: <path>: <reason>`. A refused or failed move prints its
     * reason on standard error. A game builds its own copy of the tool by calling this from its own `main` with its own
     * registration, so the files of its types are scanned rather than listed as unscanned.
     *
     * @param registrations Registrations to run after the engine's, a game's own among them.
     * @return 0 when the file moved (or a dry run found it could), 1 when the move was refused and nothing was written,
     * 2 for arguments it cannot read, and 3 when a write failed partway.
     */
    [[nodiscard]] int MoveAssetMain(int argc, const char* const* argv,
                                    std::initializer_list<TypeRegistration> registrations = {});
} // namespace Fenrir
