# 03: `PanoramaSky` draws an HDR panorama as the sky

**What to build:** A third `Sky` kind takes a flat equirectangular HDR image, converted on the GPU to a cubemap once per image revision; `CubemapSky` and `PanoramaSky` gain `intensity` and `rotation` (spec: "Lights and the sky"; ADR-0047).

- **`PanoramaSky { image, intensity, rotation }`** in the `Sky` union; an image that is not flat or not twice as wide as high is warned about once and the sky falls back to the default colour.
- **A render graph pass converts the panorama** into a cached cube texture (face size from the image's height, with mips), redone only when the image or its revision changes. `Background` draws it like a `CubemapSky`.
- **`intensity` and `rotation`** (about world +Y) apply to both cubemap and panorama skies.
- The API description gains the new kind (generated output only).

**Blocked by:** 01

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`

**Status:** ready-for-agent

- [ ] `RendererProbe` on null: the conversion runs once per revision and again after the image changes.
- [ ] GL pixel probes: a synthetic panorama with a distinct colour per direction lands on the right cube faces; `rotation` turns it.
- [ ] Golden image: a panorama sky behind a lit sphere.

Hand check: a Poly Haven panorama looks seamless when flying around under it.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
