# How are cameras, viewports and render targets modelled?

Type: grilling
Status: open
Blocked by: 14

## Question

How does game code say what is drawn from where, and into what?

- A camera component on an entity (its world transform as the view), projection settings, which camera is active; `FenrirCamera` removed or reshaped.
- Several cameras: split screen, picture in picture, a camera drawing into a texture (which an editor's scene view would need).
- Viewports, resolution, aspect ratio and resizing.
- Screen-to-world rays for picking, and where that helper lives.
