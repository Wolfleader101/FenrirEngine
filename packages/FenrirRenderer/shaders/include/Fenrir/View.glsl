// SPIKE: set 1, the view (ADR-0024). Reversed-Z, infinite far, upper-left origin (ADR-0016, ADR-0026).
layout(set = 1, binding = 0, std140) uniform ViewData
{
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 cameraPosition;
    vec4 viewport; // x, y, width, height in pixels
} view;
