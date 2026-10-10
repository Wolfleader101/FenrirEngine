---
status: accepted
---

# Build our own engine-independent .NET hosting library before engine integration

FenrirScripting will become a general-purpose C++/CoreCLR hosting library, comparable in scope to Mono's embedding API and Coral. It will offer type and method handles, typed invocation, managed objects with explicit ownership, fields, strings, structs, arrays, internal calls, attributes, and collectible reload. Its core must be usable from any C++ project, not just FenrirEngine. Gameplay/ECS integration starts only once that API is stable, so engine requirements can't bend the hosting lifetime model before it has been proven.

We write it ourselves rather than adopting Coral or Mono:

- **Mono** is the legacy runtime. We want CoreCLR and its official hosting APIs.
- **Coral** fits the shape we want, but we want to own and understand every lifetime and ABI contract. We use it only as an architectural reference and copy none of its code.

## Consequences

- This replaces the earlier plan's rule against "a general reflection framework or arbitrary marshalling until a requirement justifies it". This ADR is that requirement. The rest of the plan's discipline stays: one narrow, verified slice at a time.
- No engine package may become a dependency of the hosting core.
- Automated CI and cross-platform runners are deliberately deferred. Verification is local, and the docs must say which platforms were actually tested.
