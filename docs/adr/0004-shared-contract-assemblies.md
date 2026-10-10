---
status: accepted
---

# Share declared contract assemblies through the default load context

Components need to agree on types they exchange with the host, such as a scripting API they compile against. If each collectible component loaded its own copy, the same type would have a different identity in every component. So the host declares shared assemblies (`HostConfiguration::sharedAssemblies`). The bridge loads each once into the default load context, and every component's reference to that name binds to the shared copy, even when the component deploys a private one. The bridge itself is always shared this way.

We chose an explicit declaration over "anything already in the default context is shared". That rule would also capture framework assemblies a component deliberately deploys a newer copy of, and would make sharing depend on load order.

## Consequences

- Sharing is process-wide and permanent, like the default load context. A shared assembly is never collectible and can't be replaced by reload; only component code reloads.
- Sharing a different file under an already shared name fails initialization.
- A reference the shared copy can't satisfy (a newer version, another culture or public key) fails loudly instead of binding to the wrong assembly.
