#version 460 core
out vec4 FragColor;
  
in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;
in vec3 LightPos;

uniform vec3 lightColor;
uniform sampler2D Tex;


void main()
{
    vec4 texColor = texture(Tex, TexCoord);

    //! LIGHTING

    // ambient
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColor;

    // diffuse
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(LightPos - FragPos);

    // dot product returns the cosine of the angle between the two vectors, if the angle is 90 degrees, the cosine is 0, so the diffuse light is 0
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    
    // specular
    float specularStrength = 0.5;
    vec3 viewDir = normalize(-FragPos); // view pos is (0, 0, 0)
    vec3 reflectDir = reflect(-lightDir, norm);

    float shininess = 32;
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    vec3 specular = specularStrength * spec * lightColor;

    vec3 result = (ambient + diffuse + specular) * texColor.xyz;
    FragColor = vec4(result, 1.0);
}