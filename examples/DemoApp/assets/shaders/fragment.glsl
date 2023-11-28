#version 460 core
out vec4 FragColor;
  
in vec2 TexCoord;

uniform sampler2D Tex;

void main()
{
   FragColor = texture(Tex, TexCoord);
}