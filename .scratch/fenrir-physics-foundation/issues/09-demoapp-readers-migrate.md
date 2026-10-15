# 09: DemoApp readers migrate

**What to build:** The DemoApp's camera controller, editor, window and AutoPlay read events through readers, so input is handled once per event no matter where the window polls (ADR-0012; spec: "DemoApp"). Migration batch for the DemoApp.

**Blocked by:** 07 (Event readers in C++).

**Status:** resolved

- [ ] No DemoApp code calls `ReadEvents`.
- [ ] AutoPlay's key events are handled once by the window (they were seen twice before).
- [ ] Camera movement, mouse look, editor selection and window input behave as before (checked by hand).

**Resolved by:** merge of impl/09-demoapp-readers (9d69623)
**Hand check pending:** camera movement, mouse look, scroll zoom, editor click-select, Escape/close/resize, AutoPlay keys handled once.
