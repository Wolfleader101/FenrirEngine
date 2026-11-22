# 09: `ReadTree` sniffing text or binary from a byte span

**What to build:** the one door loaders call (ADR-0085 §2). Given a file's bytes, it reads the tree whether they are JSON5 text or the tree encoding, so FenrirAssets never knows which it got.

- **`ReadTree(std::span<const std::byte>, std::string_view path, ILogger&) -> ReadTreeResult`** (the spec, "The tree encoding and `ReadTree`"):
  - A first byte of zero goes to `DecodeTree`, since no JSON5 text can start with one.
  - Anything else goes to `ReadText` as UTF-8, skipping a BOM.
  - An empty span is one diagnostic.
- **No envelope helper** (the spec, "Gaps settled here"). `README.md` for the package shows how a caller composes the doors `ReadTree`, `Upgrade`, `FromTree`, `ToTree` and `WriteText` with keys of its own. A game's save file is the example.

**Blocked by:** 07, 08.

**Touches:** `packages/FenrirSerialisation/` (`include/FenrirSerialisation/`, `src/`, `Tests/`, `Tests/CMakeLists.txt`, `README.md` new)

**Status:** ready-for-agent

- [ ] `ReadTree` on a golden's text bytes and on its encoded bytes gives trees that are equal, trivia aside.
- [ ] Text with a BOM reads, and an empty span gives one diagnostic.
- [ ] The package README's composition example compiles, as a test that runs it.
