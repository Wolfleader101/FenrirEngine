# What dev-time data does the engine hold, and in what files?

Type: grilling
Status: open
Blocked by: 07, 12

## Question

What does the development workflow need in files that is not game content, and how does each relate to the data asset format?

- **Project settings:** whether the `Install(App&, Config)` configurations (renderer, window, physics, assets, scripting) can come from a project file, which settings belong there versus in a scene's resources, how they are layered (engine defaults, project, platform, per-developer), and whether the shipped game reads the same file or a cooked one.
- **Import sidecars:** a file beside a source model or image holding import options (scale, axes, which sub-assets to keep, texture settings, sampler defaults); whether it also carries the asset's stable identity (as [How are assets identified?](11-asset-identity-and-references.md) decided), what happens when it is missing, and where imported results are cached if at all (today imports run at load).
- **Per-developer overrides:** a local file ignored by version control for asset roots, debug flags and device choice; its name and precedence.
- **Same format, same registry:** whether these are data assets in all but role (described types, the one text format) or plain configuration with their own readers.

Zoom into [How do engines hold dev-time data?](07-dev-time-data-in-engines.md) and [Which text format do files use?](12-which-text-format-and-versioning.md).
