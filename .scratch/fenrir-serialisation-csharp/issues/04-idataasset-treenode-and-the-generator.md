# 04: `IDataAsset`, the attributes, `TreeNode` with its reader and writer, and `DataAssetGenerator`'s entry, codecs and table

**What to build:** a C# game defines a data asset type as a class, and the generator writes everything needed to move it to and from a tree (ADR-0083, ADR-0075, ADR-0074). See the spec's "Defining a C# data asset type" and "The data asset generator".

- **In `Fenrir.Scripting`:**
  - `IDataAsset`;
  - `[Version]`, `[Formerly]`, `[Upgrader]`, `[Path]`;
  - the metadata attributes: `[Range]`, `[Step]`, `[DisplayName]`, `[Group]`, `[ReadOnly]`, `[Hidden]`, `[Multiline]`, `[Units]`;
  - `[NativeDataAsset]`;
  - `TreeNode`, with a `TreeReader` and `TreeWriter` over the tree encoding (ADR-0085). Pin these byte for byte against `EncodeTree` and `DecodeTree` with shared fixtures;
  - `DataAssetCodecs`, keyed by `Type`.
- **`DataAssetGenerator`.** It emits, per `IDataAsset` class:
  - the description entry, whose default is `new T()` through the codec;
  - `ToTree` and `FromTree`, base fields first, with absent fields defaulted and unknown fields dropped;
  - the `[Upgrader]` registration.

  It skips classes marked `[NativeDataAsset]`.
- **`DataAssetTable`.** A generated table beside `ComponentTable` and `EventTable`, with its names in `GeneratedNames`.
- **Diagnostics.** Each is a `FENRIR00xx` error at the member, using free numbers from 0006 up, for:
  - a struct `IDataAsset`;
  - a refused base;
  - a refused field kind;
  - an `Entity` field;
  - a class field held by value;
  - a non-static or mis-typed `[Upgrader]`;
  - two upgraders for one version;
  - a misplaced `[FixedString]`;
  - a generic or inaccessible class.
- **Compile-fail cases** (`fenrir_expect_compile`):
  - a `struct IDataAsset`;
  - an `Entity` in a data asset;
  - a class field by value;
  - a non-static `[Upgrader]`.

**Blocked by:** 03.

**Touches:** `packages/FenrirScripting/Managed/` (`Fenrir.Scripting/`, `Fenrir.Scripting.Generators/`, the generator tests and compile-fail projects), `packages/FenrirScripting/Tests/` (encoding fixtures shared with C++)

**Status:** resolved

- [x] A fixture class extending another C# class, with every allowed kind, round-trips through its codecs, and a field missing from the tree takes its default.
- [x] `TreeWriter` output equals `EncodeTree`'s for the shared fixtures, and `TreeReader` reads `EncodeTree`'s back.
- [x] Each diagnostic fires at the member it names, and each compile-fail case fails as expected.
