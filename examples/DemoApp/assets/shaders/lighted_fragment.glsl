#version 460 core
out vec4 FragColor;
  
in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

uniform vec3 lightColor;
uniform sampler2D Tex;
uniform vec3 lightPos;
uniform vec3 viewPos; // TODO in the future we might want to calculate all lighting in view space not world space

void main()
{
    vec4 texColor = texture(Tex, TexCoord);

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);

    // dot product returns the cosine of the angle between the two vectors, if the angle is 90 degrees, the cosine is 0, so the diffuse light is 0
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColor;

    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);

    float shininess = 32;
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    vec3 specular = specularStrength * spec * lightColor;

    vec3 result = (ambient + diffuse + specular) * texColor.xyz;
    FragColor = vec4(result, 1.0);
}