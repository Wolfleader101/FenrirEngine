# Building a game against the engine

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [What breaks when the engine is built as a subdirectory?](../engine-gaps/issues/16-build-the-engine-as-a-subdirectory.md) and [What CMake functions does the engine give a game?](../engine-gaps/issues/17-what-cmake-functions-a-game-gets.md). Settles [A game cannot build its scripts or copy its assets without demo code](../engine-gaps/issues/02-building-a-game-against-the-engine.md).

## Problem Statement

A game is a CMake project of its own that adds FenrirEngine with `add_subdirectory`, from a submodule or a path, with the demo apps off. Today that game cannot get off the ground:

- Configuring fails as soon as it has C# scripts: the Iris bridge's directory is a plain variable that never reaches the game's scope, so deploying scripting stops with an error.
- Its own material shaders fail to build: the shader include directory is a plain variable in the engine's scope, so the shader compiler is handed an empty include path.
- Its C++ code does not get C++20 unless it happens to link a target that carries it, and a target that links only the app package fails to compile.
- The engine's build outputs land in the game's build root, where their names could clash with the game's.
- The only function that builds a C# script project and deploys it, with its contracts and scripting's run-time files, beside an executable is the demo scaffold's, which always adds the demo contract and the demo Razor components and does not exist with the demo apps off. The only asset copying is inside the demo app function, mixed with the shared demo assets.
- An editor opening the game's C# project sees none of the engine's contracts.
- When a contract is missing beside the executable, .NET fails to start silently and the app runs on without its scripts.
- Nothing says how a game consumes the engine, and nothing checks that it still can.

Flightless Frenzy, the first game, needs all of this fixed in the engine before its first ticket; it may not copy demo code or work around the engine in its own build.

## Solution

The engine gives a game two CMake functions, one to build and deploy a C# script project beside an executable and one to copy asset folders beside it, available to any project that adds the engine with `add_subdirectory`. The demo scaffold's functions become thin callers of them, passing only the demo parts. The engine's CMake stops assuming it is the top-level project: every path and setting a game's targets need reaches them, and the engine's outputs stay under its own build directory. The script function writes a props file that a game's C# project imports in one line, so editors see the contracts. A missing contract is logged as an error. The README gains a section on building a game against the engine, and a small outside project in the repo proves it, run by hand.

## User Stories

1. As a game developer, I want to add the engine to my CMake project with `add_subdirectory`, so that my game builds against it from a submodule or a path without installing it.
2. As a game developer, I want configuring my project with the engine as a subdirectory to succeed with scripting on, so that I can start writing C# at once.
3. As a game developer, I want to compile my own material shaders with the engine's shader function from my own directory, so that my game's look is mine.
4. As a game developer, I want every engine target I link to give my target C++20, so that I do not have to know which engine target happens to require it.
5. As a game developer, I want the engine's generated outputs kept under the engine's own build directory, so that they cannot clash with my build's files.
6. As a game developer, I want my own warning level and compile-commands settings to apply to my code, so that the engine does not decide them for me.
7. As a game developer, I want one function that builds my C# script project against the contracts I name, so that I do not repeat the demo scaffold's long list of properties.
8. As a game developer, I want that function to deploy my script assembly, its dependency file, the contracts and scripting's run-time files beside my executable, so that the game finds its scripts when it runs.
9. As a game developer, I want to name only the contracts my scripts use, so that my game deploys no more than it needs.
10. As a game developer, I want Razor support deployed when my script project has Razor components, so that my HUD and menus work without extra setup.
11. As a game developer, I want my scripts rebuilt when a C# or Razor source changes, so that a build is always current.
12. As a game developer, I want my script project's intermediate files kept out of my source tree, so that my repository stays clean.
13. As a game developer, I want a generated props file I import in my C# project with one line, so that my editor sees the engine's contracts and the scripting SDK after the first configure.
14. As a game developer, I want one function that copies one or more asset folders beside my executable under a folder name I choose, so that my game finds its assets at run time.
15. As a game developer, I want edited assets copied again on the next build, so that I see my changes without a clean build.
16. As a game developer, I want a clear error naming a missing contract when scripting cannot start, so that I know at once why my scripts did not load.
17. As a game developer, I want a README section that lists the options to set before `add_subdirectory`, the C++ standard, the .NET SDK, submodule transport and the two functions, so that I can set up a game without reading the engine's CMake.
18. As a game developer on Windows, I want the README to tell me to give the engine a short binary directory, so that a nested build does not pass MSVC's path limit.
19. As a game developer, I want the demo apps off by default in my project, or told plainly how to turn them off, so that I do not build demo code I do not use.
20. As an engine maintainer, I want the demo scaffold's functions to call the same two functions, so that the demo apps prove them every time they build.
21. As an engine maintainer, I want the demo apps' smoke runs to pass unchanged after the refactor, so that I know the scripts and assets still reach every demo app.
22. As an engine maintainer, I want a small outside project in the repo that adds the engine as a subdirectory, compiles a shader, links every package and builds and loads a C# script, so that I can check by hand that a game can still build against the engine.
23. As an engine maintainer, I want a fast CMake test of the asset-copy function, so that CI catches a broken copy without building the engine twice.
24. As an engine maintainer, I want the engine's CMake to pass values to callers through cache variables, target properties or the function's own location rather than directory variables, so that it behaves the same as the top-level project and as a subdirectory.
25. As the Flightless Frenzy developer, I want all of this in the engine before the game's first ticket, so that the game needs no demo code and no workarounds of its own.

## Implementation Decisions

- **A new engine CMake module, FenrirGame,** included by the engine's root so that a parent project has its functions after `add_subdirectory`. It is not part of the demo scaffold and exists with the demo apps off.
- **`fenrir_add_scripts(<target> PROJECT <csproj> CONTRACTS <names>...)`**
  - Builds the C# project with the .NET SDK against the scripting SDK and the named contracts (by package name: Input, Window, Assets, Renderer, Physics, UI and the rest), each passed by its contract directory as the demo scaffold passes them today.
  - When the project has Razor components, also passes the UI's Razor contracts and imports the engine's Razor props.
  - Keeps the project's intermediate files under the build directory.
  - Rebuilds when any C# or Razor source of the project, the project file or a named contract changes.
  - Deploys the script assembly and its dependency file into a `scripts` folder beside the target, and through the existing scripting deploy function the named contracts and scripting's run-time files (the .NET host, the Iris bridge, the scripting and math assemblies) beside the target.
  - Gives the target's sources the script assembly's path relative to the executable, as the demo scaffold does today.
  - Writes a props file into the build directory listing the scripting SDK's props and each named contract's path; the game's project imports it with one line, so an editor resolves the contracts after the first configure.
  - Unknown arguments and a missing project file are configure errors, as in the demo functions.
- **`fenrir_copy_assets(<target> DIRECTORIES <dirs>... DESTINATION <folder>)`** copies each directory, relative to the calling list file, into the named folder beside the target after every build, in order, so a later directory's file replaces an earlier one's of the same name.
- **The demo scaffold's functions become callers.** The demo app function copies the shared demo assets, the demo UI kit's files and the app's own through `fenrir_copy_assets`. The demo scripts function calls `fenrir_add_scripts` with the scaffold's contract list, the demo contract and the demo Razor components. Their documented behaviour is unchanged.
- **Values reach a parent project:**
  - The Iris bridge's directory and the shader include directory are found from the defining function's own location or held in cache variables, never in plain directory variables.
  - C++20 is a `PUBLIC` compile feature on the engine's targets, so every consumer gets it, not only those linking Iris.
  - Every engine output written under the top build directory today (the API description tool's output, the Iris bridge, the scripting SDK, each package's contract, Catch2) moves under the engine's own project build directory.
  - Warning levels and compile-commands export stay set for the engine's own directory only; a game sets its own.
  - The deferred checks queued on the top source directory and Iris's top-level check already behave correctly as a subdirectory and stay.
- **A missing contract is an error.** When the scripting host fails to start because a contract assembly it needs is absent, it logs an error naming the contract, instead of running on silently without scripts.
- **No engine change for path length.** The overflow is inside glslang's own tree when the engine's binary directory is nested; the README tells a game to give the engine a short binary directory (`add_subdirectory(<engine> f)`), and a deep worktree uses the existing short-path script.
- **The README** gains a "Building a game against the engine" section: the options to set before `add_subdirectory` (`FENRIR_BUILD_EXAMPLES` off, `FENRIR_ENABLE_SCRIPTING` on for C#, the rest as wanted), C++20, the .NET 10 SDK, the short binary directory, fetching submodules with `protocol.file.allow=always` (and `core.longpaths` for HarfBuzz on Windows), the two functions, and the one-line props import. ADR-0071 is untouched: only build helpers move into the engine, and the demo UI kit stays in the scaffold.

## Testing Decisions

- **A good test here checks what a game sees,** files beside its executable, a configure that succeeds, scripts that load, an error that names its cause, never how the functions are written.
- **The demo apps' existing CTest smoke runs** are the main regression check: every demo app's null-backend smoke run builds, deploys and loads its scripts and assets through the new functions once the demo functions call them, and must pass unchanged. CI already runs them.
- **A new case in the engine's CMake script tests for `fenrir_copy_assets`,** in the style of the existing fetch-demo-assets tests (a small project configured and built by a CMake script run from CTest): a project that includes only the FenrirGame module with a dummy executable, checking that each directory's files land in the destination folder, that a later directory overrides an earlier one, and that an edited file is copied again on the next build. It needs no engine build and runs in CI.
- **An outside project, kept beside those tests in an `OutsideProject` folder and run by hand,** not registered in CTest and not in CI: it adds the engine with `add_subdirectory` and a short binary directory, compiles one material shader with the engine's shader function, links every package, and builds and loads one C# script through `fenrir_add_scripts`, with no workarounds. It is run before a game's first ticket and after changes to the engine's CMake. The throwaway version on the `prototype/engine-as-subdirectory` branch is the starting point, with its two workarounds removed.
- **A FenrirScripting test** starts the host with a contract assembly missing and checks that an error naming that contract is logged; the existing scripting host tests are the prior art.
- `fenrir_add_scripts` has no test of its own beyond the demo smoke runs and the outside project, both of which build and load scripts through it.

## Out of Scope

- Installing, exporting or packaging the engine (`install`, `find_package`, CPack): a game adds the engine with `add_subdirectory`.
- Lifting the demo UI kit or any other part of the demo scaffold into the engine (ADR-0071).
- A CI job that builds the outside project or a game.
- Fixing glslang's path length inside a nested binary directory.
- Flightless Frenzy's own build, presets and host.
- Registering material shaders or renderer configuration from C#.

## Further Notes

- The facts behind each decision, with file and line, are in [What breaks when the engine is built as a subdirectory?](../engine-gaps/issues/16-build-the-engine-as-a-subdirectory.md) and the throwaway project's `FINDINGS.md` on the local `prototype/engine-as-subdirectory` branch.
- Build and test only `msvc-x64-debug` locally, through the MSVC script and, in a worktree, the short-path junction (`README.md`, "Building and testing").
