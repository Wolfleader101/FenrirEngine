# 01: One CaptureTarget for every UI test, INFO before every REQUIRE, and label sizes checked against a paragraph of the test's own

**What to build:** the UI tests' support in one place and their checks per `CODING_STANDARDS.md`, before the stack adds more tests (spec: "Cleanups"). A refactor of tests only: no product code changes, and every test that passed passes.

- **`FenrirUITestSupport`:** an INTERFACE library of FenrirUI's, built with its tests, holding `CaptureTarget`: a `UITarget` that records each `DrawList2D` it is given, with a size, a blend space and a scale rule set by the test. Today's three copies (`packages/FenrirUI/Tests/UITestApp.hpp`, `packages/FenrirUI/Scripting/Tests/UIScriptingApp.hpp`, `examples/UIDemo/Tests/DemoApp.hpp`) are compared first; the shared one takes the union of what they offer, each option documented, and the copies are deleted. FenrirUI's tests, the UI scripting tests and UIDemo's tests link it.
- **INFO first:** every test case in `packages/FenrirUI/Tests/` and `packages/FenrirUI/Scripting/Tests/` whose setup REQUIREs come before its first INFO (about 60, found by reading each case's first REQUIRE) gets an INFO above them saying what the setup was meant to give ("the stylesheet parses with no errors", "the root has a button to click").
- **`Text.cpp`'s label sizes:** "A label's laid-out size is its paragraph's…" and its siblings take the expected size from a paragraph the test lays out itself through FenrirText with the label's face, size and scale, or from a literal, not from `ParagraphOf(label)`.

**Blocked by:** None - can start immediately

**Touches:** `packages/FenrirUI/Tests/`, `packages/FenrirUI/Scripting/Tests/`, `packages/FenrirUI/CMakeLists.txt`, `examples/UIDemo/Tests/`

**Status:** resolved

- [x] `CaptureTarget` is defined once; `grep -rn "class CaptureTarget"` finds one definition.
- [x] FenrirUI's tests, the UI scripting tests and UIDemo's tests pass, the same cases as before.
- [x] No test case in the two FenrirUI test folders has a REQUIRE before its first INFO.
- [x] `Text.cpp`'s label-size checks fail when the label's content box is made a pixel wider (checked by hand once, reverted).
- [ ] Clang and MSVC x64 debug build without new warnings. (MSVC x64 debug: none. Clang: not built; no local clang build was run and CI's Linux job is off.)
