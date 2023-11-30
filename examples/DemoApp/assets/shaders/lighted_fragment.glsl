#version 460 core
out vec4 FragColor;

struct Material {
    sampler2D diffuse;
    sampler2D specular;
    float shininess;
};

struct Light {
    vec3 pos;
    vec3 direction;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    float constant;
    float linear;
    float quadratic;

    float cutOff;
    float outerCutOff;
};

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoords;

uniform Material material;
uniform Light light;
uniform mat4 view;


void main()
{
    //! LIGHTING

    // convert light pos to view space
    vec3 lightPos = vec3(view * vec4(light.pos, 1.0));
    
    // ambient
    vec3 ambient = light.ambient * texture(material.diffuse, TexCoords).rgb;

    // diffuse

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    // vec3 lightDir = normalize(-light.direction); // direction from the fragment to the light source (for directional lights)
    
    // dot product returns the cosine of the angle between the two vectors, if the angle is 90 degrees, the cosine is 0, so the diffuse light is 0
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = light.diffuse * diff * texture(material.diffuse, TexCoords).rgb;
    
    // specular
    vec3 viewDir = normalize(-FragPos); // view pos is (0, 0, 0) as its in view space
    vec3 reflectDir = reflect(-lightDir, norm);

    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = light.specular * spec * texture(material.specular, TexCoords).rgb;

    // spotlight with soft edges
    float theta = dot(lightDir, normalize(-light.direction));
    float epsilon = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    
    diffuse *= intensity;
    specular *= intensity;
    
    // attenuation is for point lights and spotlights
    float distance = length(lightPos - FragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));

    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;

    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}