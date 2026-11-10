# How do edited files reload in a running development build?

Type: grilling
Status: open
Blocked by: 15

## Question

When a data asset or scene file changes on disk while the app runs, what happens?

- **Watching:** who watches the asset root (the precedent is the script reload watcher in FenrirScripting), on which thread, and whether it is a development-only feature behind a config switch.
- **Data assets:** reload in place (same handle, revision bump, every user sees the new values, as material edits do) or a new instance; what game code that cached values sees; a broken edit (parse error) keeps the old instance and reports.
- **Scene files:** reload the whole scene (destroy and recreate, losing runtime state), patch the running scene (entities matched by file identity, changed components rewritten, added and removed entities applied), or offer both; what happens to entities made at runtime and to components the file does not write.
- **Prefabs:** an edited prefab source and its instances in a running scene.
- **Type changes:** a C++ rebuild is a restart; a C# script reload changing a type's shape while files hold the old shape.
- **Scope:** what is in the engine and what is a tool's.

Zoom into [Who loads and saves files?](15-load-save-api-and-ownership.md) and the reload findings of [What can the engine describe and name today?](08-what-the-engine-can-describe-today.md).
