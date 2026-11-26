# 10: `Assets::Move` with `MoveResult` and the dry run

**What to build:** a file under the asset root can be moved or renamed without breaking anything that names it. Every reference to it, and to its sub-assets, is rewritten across the root's envelopes, with comments and every other byte kept (ADR-0076, ADR-0079).

- **`Assets::Move(root, from, to, registry, dryRun) -> MoveResult`:** needs no running store. It does all of this:
  - It moves or renames one file, and refuses an existing destination.
  - It scans every `.json5` envelope under the root, using the registry's `reference` kinds to know which strings are asset paths.
  - It rewrites `from` and every `from#name`, the moved file's own references included.
  - It re-emits each changed file through `ReadText`, the tree edit and `WriteText` with the tree's own trivia, so comments and every other byte survive.
- **`MoveResult`:**
  - It lists each file changed.
  - It lists each file left unscanned: one whose `type` the given registry lacks, or one that does not parse.
  - A dry run writes nothing and lists the same changes.

**Blocked by:** 06.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/Assets.hpp` or a new `include/FenrirAssets/Move.hpp`, a new `src/Move.cpp`, `Tests/` (a new `Move.cpp` over a temporary root))

**Status:** resolved

- [x] Moving a sword referenced by two files and by its own sub-asset rewrites all three references. Every other byte in those files stays the same.
- [x] A dry run reports the same files and writes nothing.
- [x] An existing destination is refused, and a file of an unregistered type is named as unscanned.
- [x] After the move, each file loads with every reference resolved.
