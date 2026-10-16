#version 460 core
out vec4 FragColor;

in vec3 Normal;

// One colour, lit by a fixed light from above and to the side, so a sphere reads as round.
uniform vec3 color;

void main()
{
    const vec3 towardsLight = normalize(vec3(0.4, 1.0, 0.6));
    float diffuse = max(dot(normalize(Normal), towardsLight), 0.0);
    FragColor = vec4(color * (0.35 + 0.65 * diffuse), 1.0);
}
