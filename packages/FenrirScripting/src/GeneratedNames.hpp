#pragma once

#include <string_view>

namespace Fenrir::Detail::GeneratedNames
{
    // The names of the types the scripting SDK's source generators write into every script assembly, which the engine
    // finds by name when a generation loads: the one place in C++ that spells them. The generators spell the same
    // names once, in Managed/Fenrir.Scripting.Generators/GeneratedNames.cs; nothing reads one from the other (ADR-0042,
    // amended 2026-10-28). A script whose generated types do not match fails to start, and the end-to-end scripting
    // tests (Tests/SystemTests.cpp, Tests/AdmissionTests.cpp, every test that loads a generated assembly) load real
    // generated assemblies, so a drift between the two files fails them.

    /** @brief The full name of the system host class, one per script assembly (see SystemHost.hpp). */
    constexpr std::string_view SystemHost = "Fenrir.Scripting.Generated.SystemHost";

    /** @brief `int PhaseMask()`: bit n is set when the script has a system in phase n. */
    constexpr std::string_view PhaseMask = "PhaseMask";

    /**
     * @brief `void ReportStages()`: calls the engine's RegisterStageSystem native once per stage system, with its
     * stage, order and group.
     */
    constexpr std::string_view ReportStages = "ReportStages";

    /** @brief `void RunStage(int group)`: the systems of one stage group, in name order, with no playback. */
    constexpr std::string_view RunStage = "RunStage";

    /** @brief `void OnCreate()`: every system's OnCreate, once the generation is loaded. */
    constexpr std::string_view OnCreate = "OnCreate";

    /** @brief `void Run(int phase)`: the systems of one phase, in order. */
    constexpr std::string_view Run = "Run";

    /** @brief `void OnDestroy()`: every system's OnDestroy, before the generation is released. */
    constexpr std::string_view OnDestroy = "OnDestroy";

    /** @brief The full name of the table that reports the script's components (see ScriptData.cpp). */
    constexpr std::string_view ComponentTable = "Fenrir.Scripting.Generated.ComponentTable";

    /** @brief The full name of the table that reports the script's C#-only events. */
    constexpr std::string_view EventTable = "Fenrir.Scripting.Generated.EventTable";

    /** @brief `static void Register()` on each table: calls the reporting natives once per type. */
    constexpr std::string_view Register = "Register";
} // namespace Fenrir::Detail::GeneratedNames
