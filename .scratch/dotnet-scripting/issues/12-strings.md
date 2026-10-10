# 12: Strings

**What to build:** UTF-8 strings work as method arguments, return values and field/property values. Native input is borrowed for the duration of the call. Results are owning `std::string`s. Null strings are distinct from empty ones.

**Blocked by:** 07

**Status:** ready-for-agent

- [ ] String arguments decode strictly. Malformed UTF-8 is rejected with `InvalidArgument` before managed code runs.
- [ ] String returns arrive as owning UTF-8 and preserve embedded nulls and non-ASCII text. There is no fixed size limit and no size-then-call-again round trip that could run managed code twice.
- [ ] A null managed string is distinguishable from an empty one on the native side.
- [ ] String fields and properties work once ticket 11 is complete (a follow-up note is acceptable if 11 is not yet done).
- [ ] Any UTF-8 coverage that ticket 03 kept fixture-only is now covered here, and that fixture path is removed.
