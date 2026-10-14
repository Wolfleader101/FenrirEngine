# 03: Nothing registers after Install: remove late registration

**What to build:** `Scripting` has no registration calls. `RegisterComponent(ComponentRegistration)`, `RegisterEvent(EventRegistration)`, and the `RegisterComponent<T, Mapping>`, `RegisterReadOnlyComponent<T, Mapping>` and `RegisterEvent<T, Mapping>` templates are removed. After Install, scripting's registry changes only when a script reload removes and adds script-defined components and events. The registry generation that C# watches still goes up on each such change.

This also removes a bug in late registration. A registration was added before it was checked and left in the registry after a failure. A failed check then shut down all of scripting, stopping the .NET host and hot reload, although the header promised only to release the script assembly. The bug goes away with the calls and is not fixed separately.

See the spec, `.scratch/scripting-extension-only/spec.md`, "After Install" and "Testing Decisions".

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] `Scripting` declares none of the five registration calls. `FindComponent`, `Reload`, `Generation`, `PendingUnloadCount`, `IsRunning`, `HasScripts` and `Shutdown` keep their meaning.
- [ ] The test that a mismatched late registration is caught at once becomes a mismatch that fails Install through an extension. It folds into the existing "a mismatched component fails the load and names the field" test if that already covers it.
- [ ] The test that a field of the wrong type is caught becomes an Install-time failure through an extension, still naming the field.
- [ ] The late duplicate-name and empty-name checks become Install failures whose log names the extension.
- [ ] The moved-from `Scripting` test no longer makes registration calls. Its other assertions are unchanged.
- [ ] The test that a lookup which failed before a registry change succeeds after it is redone over a reload that adds a script-defined component, using the existing reload tests and their rebuilt fixture assemblies as prior art.
- [ ] The scripting README has no late-registration examples or asides, and says nothing registers after Install.
- [ ] ADR-0009 gains a consequence line: nothing registers after Install, and the registry changes afterwards only through a script reload. A reintroduced late registration would need the same all-or-nothing rule as a reload. The decision itself is unchanged.
- [ ] Every scripting test passes.
