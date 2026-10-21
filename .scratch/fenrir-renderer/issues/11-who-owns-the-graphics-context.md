# Who owns the graphics context and the swapchain or surface?

Type: grilling
Status: open
Blocked by: 08, 09

## Question

Who creates and owns the OpenGL context today, and the swapchain or surface a Vulkan, D3D or Metal backend would need later? **ADR.**

- Does FenrirWindow know about graphics APIs at all (creating a GL context, a Vulkan surface), or does it hand out a neutral native window and let the graphics backend make the context or surface?
- Who presents (swaps buffers), and who handles resizing, minimising and vsync?
- Is there one window or several, and can the renderer draw to more than one?
- How does `Install` order go (window before renderer), and who tears down what at exit?
