# 06: `Upgrader`, `RegisterUpgrader` and `Upgrade` in order, no downgrade

**What to build:** a tree written at an older `@version` is migrated to the type's version before `FromTree`, by per-type upgraders run in order (ADR-0077 §6). The upgraders hang on the registry as an attachment this package defines, so FenrirTypes stays tree-free.

- **`Upgrader`** is `void(std::uint32_t fromVersion, Value& tree)`.
- **`RegisterUpgrader(TypeRegistry&, typeName, fromVersion, Upgrader, ILogger&) -> bool`** adds the upgrader to the type's list, an attachment keyed by `Fenrir::Serialisation::UpgradersTag`, kept in `fromVersion` order (the spec, "Upgraders" and "Gaps settled here"). Registering for a type the registry lacks, from a `fromVersion` of 0 or at or above the type's `version` (the message naming the type, the `fromVersion` given and the type's version; decided with the user on #436), a second upgrader for one `fromVersion`, or an empty one, is one error through the logger and adds nothing (the registry's own diagnostics function is private, so the logger is passed; decided with the user on #436). A former name finds the type, as `Find` does.
- **`Upgrade(const TypeRegistry&, typeName, std::uint32_t fromVersion, Value& tree, std::string_view path, ILogger&) -> UpgradeResult`** runs each step from `fromVersion` up to the type's `version`.
  - A missing step is one error, and the tree stays at the version it reached.
  - A tree newer than the type is read as it is, with one warning.
  - The caller passes `fromVersion`, so the package never names an envelope key.
  - `UpgradeResult` is `status`, the `version` reached and `diagnostics`, each logged once and located at the tree's root (1:1 for a tree built in code), with `path` naming the file as `FromTree`'s does (decided with the user on #436).

**Blocked by:** 04.

**Touches:** `packages/FenrirSerialisation/` (`include/FenrirSerialisation/`, `src/`, `Tests/`, `Tests/CMakeLists.txt`)

**Status:** resolved

- [x] A `sword` tree at `version: 1` with a renamed field reaches version 3 through two registered upgraders, run in order, and `FromTree` then reads it.
- [x] A missing step gives one error and leaves the tree at the version it reached. A tree newer than the type gives one warning and stays unchanged.
- [x] Registering for an unknown type, or registering twice for one `fromVersion`, is one error and changes nothing.
- [x] Registering from version 0, or from at or above the type's version, is one error and changes nothing.
