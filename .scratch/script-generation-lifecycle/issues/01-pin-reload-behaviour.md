# 01: Pin reload behaviour through the existing seam

**What to build:** Tests over `Scripting::Install` / `Reload` (interval and debounce from config, as the current reload tests do) that pin reload behaviour before the extraction. They pass against the code as it is when this starts.

**Blocked by:** `script-schema-reconciliation` ticket 04

**Status:** resolved

- [ ] A rebuilt file is picked up between frames
- [ ] A file that keeps changing is not loaded until stable
- [ ] A bad new build rolls back to the previous one
- [ ] A bad new build with a bad previous one leaves no scripts and says so
- [ ] Old copy deleted once collected, kept while pending
- [ ] Hot reload off: no automatic reload, `Reload()` still works
- [ ] A reload with nothing loaded fails cleanly

Spec: `.scratch/script-generation-lifecycle/spec.md`
