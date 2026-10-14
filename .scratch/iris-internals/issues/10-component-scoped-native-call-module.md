# 10: One native module for component-scoped calls

**What to build:** The rules of a call that belongs to one component are written once on the native side (spec: "The native component-scoped call module").

One source-private module holds: that an object argument is valid and belongs to this component; adopting a returned object handle into an owner; taking a returned string block; taking a returned struct block; and freeing a block however the call ends. The method, member and array handles delegate to it, and invoking a static method and an instance method share their body. The public `Method`, `InstanceMethod`, `Member` and `Array` templates keep their interface and behaviour.

**Blocked by:** 02 (Fake-bridge checks for the remaining native paths).

**Status:** resolved

- [x] The module exists, the three handle kinds delegate to it, and invoking static and instance methods share one body.
- [x] The "object is valid and belongs to this component" check is written once.
- [x] The per-handle adopt and take declarations that existed so one template could call three handle types are gone from the public headers' `Detail` section. Nothing outside `Iris::Detail` changes in the public headers.
- [x] The fake-bridge checks from the earlier tickets pass unchanged, and the `Components` and consumer-package checks pass unchanged.
- [x] No ownership or lifetime rule changes: owners, handles, tokens, release and forced release are as `GLOSSARY.md` and ADR-0005 say.
- [x] The full CTest suite passes.

## Comments

Done. `Native/src/ComponentCall.{hpp,cpp}` is the source-private module: `ObjectProblem` (the one check that an object is valid and belongs to the component, with the argument position or "the object" deciding the words), `ResolveObjects` and `TargetHandle` built on it, and `CompleteReturningSlot` (a returned block is given back if completing the call throws; it was written out three times). Taking a returned object, string or struct is `Detail::CallOrigin`, declared in `Object.hpp`: each raw handle now has one `Origin()` (the array one takes the array object) where it had three adopt/take declarations, and the public templates call `handle.Origin().Adopt/TakeString/TakeStruct`. Invoking a static and an instance method is one body in `Method.cpp`. `ResolveObjects` and `Member`'s `TargetHandle` left the public headers.

Public headers: the changes are in `Iris::Detail` (the new `CallOrigin`, forward declarations, the raw handles) plus two friend declarations inside `Object`'s private section so the module and `CallOrigin` can reach its state; nothing a caller can use changed. No ownership or lifetime rule changed. New fake checks pin the words of every object problem (a released and a foreign argument with its position, a released and a foreign instance target, and the same for a member target), and a mutation to one of them fails two cases. Iris 14/14 and the engine suite 38/38 pass.
