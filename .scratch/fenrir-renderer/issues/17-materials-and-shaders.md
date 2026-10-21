# What are materials, and how do they bind shaders and parameters?

Type: grilling
Status: open
Blocked by: 12, 15

## Question

What is a material in FenrirRenderer, and how does it reach a shader?

- The foundation's two materials: unlit, and one simple lit material (one directional light and ambient, no shadows), built through the same general path the lighting spec will extend.
- Is a material an asset shared by many entities, a component value, or both (instances with overrides)?
- How parameters (colours, scalars, textures) are named and set, from C++ and C#, and how a user's own shader is added.
- Shader variants (lit or unlit, later skinned), and render state (blending, culling, depth).
- Where the directional light and ambient live (a component, a scene resource, renderer config).
