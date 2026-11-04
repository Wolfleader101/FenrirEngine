# 24: Atmospheric haze and sun shafts

**What to build:** Atmospheric depth: distance haze and light shafts through the trees, as in the forest clearing's reference images. Requested by the user while reviewing ticket 16 (the forest clearing), 2026-11-01. The renderer has no fog of any kind today; the forest leans on bloom and the panorama's haze.

- **Height fog** per camera or scene: density falling off with height, extinction and in-scattering of the sky's light by distance, so far trees fade towards the horizon's colour.
- **Sun in-scattering, shadowed**: the sun's light scattered towards the camera with a phase function (Henyey-Greenstein), sampled against the sun's shadow cascades along each view ray, so the gaps between trunks make shafts. A froxel volume (Wronski 2014, Hillaire 2015) or a half-resolution ray march with temporal or bilateral filtering; the choice wants an ADR.
- Applied after Opaque, before bloom and tonemapping; transparent items fogged in their own shading.
- LightingDemo's panel gains the fog's density, height falloff and scattering, and the forest clearing turns it on.

**Blocked by:** 15, 16

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `docs/adr/`, `examples/LightingDemo/`

**Status:** needs-triage

- [ ] CPU unit tests: the fog's transmittance by distance and height matches its closed form.
- [ ] GL pixel probes: a far surface fades towards the fog's colour; a shadowed ray scatters less sunlight than a lit one.
- [ ] A golden image of a fogged scene with a shadow caster in the sun's path.

Hand check: in the forest clearing, shafts of light fall through the gaps between the trees towards the camera, and the far forest fades into haze.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
