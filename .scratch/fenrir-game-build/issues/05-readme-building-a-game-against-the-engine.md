# 05: README: building a game against the engine

Spec: [Building a game against the engine](../spec.md).

**What to build:** The README gains a "Building a game against the engine" section: the options to set before `add_subdirectory` (`FENRIR_BUILD_EXAMPLES` off, `FENRIR_ENABLE_SCRIPTING` on for C#, the rest as wanted), C++20, the .NET 10 SDK, a short binary directory for the engine (`add_subdirectory(<engine> f)`) so a nested build stays under MSVC's path limit, fetching submodules with `protocol.file.allow=always` (and `core.longpaths` for HarfBuzz on Windows), the two functions with their arguments, the one-line props import, and the outside project as the worked example.

**Blocked by:** 01, 02, 03
**Touches:** `README.md`, `cmake/Tests/OutsideProject/`

**Status:** ready-for-agent

Top of the stack, on 03.

- [ ] Following the section alone, the outside project configures, builds and runs with its C# script loaded.
- [ ] The section names the outside project as the example and says it is run by hand.
