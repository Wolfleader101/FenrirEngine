#include "TextureLibrary.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb_image.h"

#include <glad/glad.h>

#include "FenrirLogger/ILogger.hpp"

TextureLibrary::TextureLibrary(Fenrir::ILogger& logger) : m_logger(logger)
{
    // TODO figure out where to put this as GL isnt intiialised yet
    // m_textures["error"] = LoadTexture("assets/textures/error.png");
}

void TextureLibrary::AddTexture(const std::string& path)
{
    if (HasTexture(path))
    {
        m_logger.Warn("TextureLibrary::AddTexture - Texture already loaded: " + path);
        return;
    }

    m_textures[path] = LoadTexture(path);
}

void TextureLibrary::ImportTexture(const std::string& path, int& width, int& height, int& nrChannels,
                                   unsigned char*& data)
{
    stbi_set_flip_vertically_on_load(true);
    data = stbi_load(path.c_str(), &width, &height, &nrChannels, 0);
}

void TextureLibrary::FreeTexture(unsigned char* data)
{
    stbi_image_free(data);
}

Texture TextureLibrary::LoadTexture(const std::string& path)
{
    Texture texture;
    texture.path = path;

    glGenTextures(1, &texture.Id);
    glBindTexture(GL_TEXTURE_2D, texture.Id);

    // set texture filter options on current texture object
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &texture.width, &texture.height, &texture.nrChannels, 0);

    if (data)
    {
        // generate the texture
        // TODO move this sort of code to renderer, and also RGB should be channels
        if (texture.nrChannels == 4)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        }
        else if (texture.nrChannels == 3)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture.width, texture.height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        }
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        m_logger.Fatal("TextureLibrary::LoadTexture - Failed to load texture: " + path);

        texture = m_textures["error"];
    }

    stbi_image_free(data);

    return texture;
}

const Texture& TextureLibrary::GetTexture(const std::string& path)
{
    if (!HasTexture(path))
    {
        m_logger.Error("TextureLibrary::GetTexture - Texture not loaded: " + path);

        m_logger.Info("TextureLibrary::GetTexture - Attempting to load texture: " + path);
        Texture texture = LoadTexture(path);

        m_textures[path] = texture;
    }

    return m_textures.at(path);
}

bool TextureLibrary::HasTexture(const std::string& path) const
{
    return m_textures.find(path) != m_textures.end();
}

void TextureLibrary::AddSkybox(const std::string& name, const std::string& top, const std::string& bottom,
                               const std::string& left, const std::string& right, const std::string& front,
                               const std::string& back)
{
    if (HasSkybox(name))
    {
        m_logger.Warn("TextureLibrary::AddSkybox - Skybox already loaded: " + name);
        return;
    }

    Skybox skybox;
    skybox.name = name;
    skybox.top = top;
    skybox.bottom = bottom;
    skybox.left = left;
    skybox.right = right;
    skybox.front = front;
    skybox.back = back;

    LoadSkybox(skybox);

    m_skyboxes[name] = skybox;
}

void TextureLibrary::LoadSkybox(Skybox& outSkybox)
{
    glGenTextures(1, &outSkybox.Id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, outSkybox.Id);

    // set texture filter options on current texture object
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    int width, height, nrChannels;
    // import the textures
    stbi_set_flip_vertically_on_load(false);

    // top
    unsigned char* data = stbi_load(outSkybox.top.c_str(), &width, &height, &nrChannels, 0);
    if (data)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    }
    else
    {
        m_logger.Fatal("TextureLibrary::LoadSkybox - Failed to load texture: " + outSkybox.top);
    }

    // bottom
    data = stbi_load(outSkybox.bottom.c_str(), &width, &height, &nrChannels, 0);
    if (data)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    }
    else
    {
        m_logger.Fatal("TextureLibrary::LoadSkybox - Failed to load texture: " + outSkybox.bottom);
    }

    // left
    data = stbi_load(outSkybox.left.c_str(), &width, &height, &nrChannels, 0);
    if (data)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    }
    else
    {
        m_logger.Fatal("TextureLibrary::LoadSkybox - Failed to load texture: " + outSkybox.left);
    }

    // right
    data = stbi_load(outSkybox.right.c_str(), &width, &height, &nrChannels, 0);
    if (data)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    }
    else
    {
        m_logger.Fatal("TextureLibrary::LoadSkybox - Failed to load texture: " + outSkybox.right);
    }

    // front
    data = stbi_load(outSkybox.front.c_str(), &width, &height, &nrChannels, 0);
    if (data)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    }
    else
    {
        m_logger.Fatal("TextureLibrary::LoadSkybox - Failed to load texture: " + outSkybox.front);
    }

    // back
    data = stbi_load(outSkybox.back.c_str(), &width, &height, &nrChannels, 0);
    if (data)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    }
    else
    {
        m_logger.Fatal("TextureLibrary::LoadSkybox - Failed to load texture: " + outSkybox.back);
    }

    stbi_image_free(data);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

const Skybox& TextureLibrary::GetSkybox(const std::string& name)
{
    if (!HasSkybox(name))
    {
        m_logger.Error("TextureLibrary::GetSkybox - Skybox not loaded: " + name);
    }

    return m_skyboxes.at(name);
}

bool TextureLibrary::HasSkybox(const std::string& name) const
{
    return m_skyboxes.find(name) != m_skyboxes.end();
}
