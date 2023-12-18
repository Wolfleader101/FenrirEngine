#version 460 core
out vec4 FragColor;
  
in vec2 TexCoord;

uniform vec3 lightColor;
uniform sampler2D Tex;

void main()
{
   FragColor = vec4(texture(Tex, TexCoord).xyz * lightColor, 1.0);
}