---
status: accepted
---

# Freeze the Iris public API at 0.2, and grow it only by addition

After the stabilisation review (`.scratch/dotnet-scripting/review-18.md`), the public headers in `packages/Iris/Native/include/Iris` are frozen as the surface FenrirEngine and other C++ projects build on. The package version is 0.2.0, and its CMake compatibility is `SameMajorVersion`, so `find_package(Iris 0.2)` accepts any later 0.x. (The switch from `SameMinorVersion` also means an old `find_package(Iris 0.1)` accepts 0.2, although 0.2 broke 0.1. No consumer outside this repository exists yet, so this is harmless.)

From now on:

- **Additive changes are allowed** in any minor release:
  - new functions, overloads and types;
  - a new status, but only for a case that previously could not happen (a new status that takes cases from an existing one changes that status's meaning, and is breaking);
  - new supported value types;
  - a default constructor;
  - a call that used to fail and now succeeds.
- **Breaking changes need their own ADR** that names what breaks and why. They are batched into a major version. Examples:
  - removing or renaming anything public;
  - changing a signature, an existing status's meaning, or a documented ownership or lifetime rule.
- **Not part of the freeze:**
  - anything in `namespace Iris::Detail`, which public templates need but is not API;
  - the bridge function table and its version, which are private and versioned by the handshake;
  - the managed bridge's internals.

Before freezing, the review removed what would have been breaking to remove later:

- the unreachable `CallStatus::BufferTooSmall`;
- the public `Array::FromObject` and `Array::AsObject`;
- private bridge helpers declared in a public header;
- a `CallResult` dereference that could dangle on a temporary;
- the `GetUnloadStatus` / `ComponentUnloadState` naming mismatch (now `GetUnloadState`).

It also fixed the meaning of `InvalidArgument` as frozen: a value that cannot cross the boundary in either direction (including a returned string that cannot become UTF-8), a malformed or ambiguous name, or a misused call.

It also deferred every missing feature (member enumeration, enums, empty handles, an unload wait, a faster invocation path), because each one is additive.

We chose 0.2, not 1.0, because no real consumer has used the API yet, and Linux and macOS have never been run. Engine integration (ticket 19) is the first real test. 1.0 should follow once it has shipped on the frozen surface without needing a breaking ADR.

## Consequences

- Ticket 19 and its follow-ups file missing hosting features as additive Iris tickets. They do not work around them in the engine, and they do not change existing behaviour.
- A pull request that touches `include/Iris` is reviewed as additive or breaking. If it is breaking, it comes with an ADR.
- `Detail` must stay clearly marked as not API, since users can see it in the headers.
