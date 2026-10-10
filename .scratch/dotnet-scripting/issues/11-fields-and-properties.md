# 11: Fields and properties

**What to build:** Native code can read and write static and instance fields and properties of the supported value types (and of object handles once ticket 10 is done) through validated, cached member handles.

**Blocked by:** 09

**Status:** ready-for-agent

- [ ] Get and set work for static and instance fields and properties of blittable primitive types.
- [ ] Type mismatches, missing members, read-only targets and wrong static/instance usage return diagnostics.
- [ ] A property getter or setter that throws returns `ManagedException`, and the object stays usable.
- [ ] Member handles are generation-bound and stale-checked, and do not keep the component alive.
- [ ] Tests cover round trips and every failure case above.
