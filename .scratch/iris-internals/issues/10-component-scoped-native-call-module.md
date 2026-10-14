# 10: One native module for component-scoped calls

**What to build:** The rules of a call that belongs to one component are written once on the native side (spec: "The native component-scoped call module").

One source-private module holds: that an object argument is valid and belongs to this component; adopting a returned object handle into an owner; taking a returned string block; taking a returned struct block; and freeing a block however the call ends. The method, member and array handles delegate to it, and invoking a static method and an instance method share their body. The public `Method`, `InstanceMethod`, `Member` and `Array` templates keep their interface and behaviour.

**Blocked by:** 02 (Fake-bridge checks for the remaining native paths).

**Status:** ready-for-agent

- [ ] The module exists, the three handle kinds delegate to it, and invoking static and instance methods share one body.
- [ ] The "object is valid and belongs to this component" check is written once.
- [ ] The per-handle adopt and take declarations that existed so one template could call three handle types are gone from the public headers' `Detail` section. Nothing outside `Iris::Detail` changes in the public headers.
- [ ] The fake-bridge checks from the earlier tickets pass unchanged, and the `Components` and consumer-package checks pass unchanged.
- [ ] No ownership or lifetime rule changes: owners, handles, tokens, release and forced release are as `GLOSSARY.md` and ADR-0005 say.
- [ ] The full CTest suite passes.
