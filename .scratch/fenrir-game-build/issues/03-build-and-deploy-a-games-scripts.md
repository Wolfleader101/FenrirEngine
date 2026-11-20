# 03: Build and deploy a game's C# scripts

Spec: [Building a game against the engine](../spec.md).

**What to build:** A game builds its C# script project and deploys it beside its executable with `fenrir_add_scripts(<target> PROJECT <csproj> CONTRACTS <names>...)` from the FenrirGame module. It builds against the scripting SDK and the contracts named by package (passing each contract's directory), adds the UI's Razor contracts and the engine's Razor props when the project has Razor components, keeps intermediate files under the build directory, and rebuilds when a C# or Razor source, the project file or a named contract changes. It deploys the script assembly and its dependency file into `scripts` beside the target and, through the existing scripting deploy function, the named contracts and scripting's run-time files beside it, and gives the target's sources the script assembly's path. It writes a props file into the build directory listing the scripting SDK's props and each named contract's path, which a game's project imports with one line so an editor resolves the contracts after the first configure. Unknown arguments and a missing project are configure errors. The demo scripts function becomes a caller passing the scaffold's contracts, the demo contract and the demo Razor components; the outside project gains one C# script built and loaded through it.

**Blocked by:** 01, 02
**Touches:** `cmake/FenrirGame.cmake`, `examples/DemoScaffold/cmake/FenrirDemoApp.cmake`, `cmake/Tests/OutsideProject/`

**Status:** ready-for-agent

Stacked on 02.

- [ ] The outside project's C# script, built with `fenrir_add_scripts` naming only the contracts it uses, loads when the outside executable runs headless.
- [ ] Its project imports the generated props file with one line and builds from an editor or `dotnet build` after the first configure.
- [ ] A project with Razor components gets the Razor contracts and props; one without them does not.
- [ ] Editing a script source rebuilds and redeploys it on the next build.
- [ ] The demo scripts function uses `fenrir_add_scripts`; every demo app's null-backend smoke run passes unchanged, scripts loaded.
