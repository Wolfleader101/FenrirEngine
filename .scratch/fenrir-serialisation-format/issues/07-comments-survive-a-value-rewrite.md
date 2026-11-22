# 07: Comments survive a value rewrite: `ToTree` with a `prior` tree

**What to build:** a tool that reads a file, changes the value and writes it again keeps the file's comments, although a struct holds none (ADR-0077: comments are kept across a rewrite). `ToTree` takes the tree the value was read from and copies trivia by key path.

- **`ToTree(..., const Value* prior)`** (the spec, "The bridge"): each node of the new tree whose key path exists in `prior` takes that node's trivia:
  - its leading comments and trailing comment;
  - a container's closing comments;
  - the root's leading comments.

  A comment on a field no longer written is dropped. Array elements match by index. With no `prior`, the tree has no trivia, as before.
- **The round trip a tool makes:** `ReadText`, `FromTree`, a change to one field, `ToTree` with `prior`, then `WriteText`. The result is the original bytes with only that field's value changed.

**Blocked by:** 05, 06.

**Touches:** `packages/FenrirSerialisation/` (`src/`, `Tests/`, `Tests/CMakeLists.txt`)

**Status:** ready-for-agent

- [ ] Each golden, read, bridged into its hand-registered types, changed in one field and written with `prior`, differs from the golden only in that field's value.
- [ ] A comment on a field that is now at its default, and so omitted, is dropped. The other comments stay.
- [ ] With no `prior`, the written tree has no comments.
