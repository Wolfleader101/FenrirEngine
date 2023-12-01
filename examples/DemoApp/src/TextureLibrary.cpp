#include "TextureLibrary.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb_image.h"

#include <glad/glad.h>

#include "FenrirLogger/ILogger.hpp"

TextureLibrary::TextureLibrary(Fenrir::ILogger& logger) : m_logger(logger)
{
    m_textures["error"] = LoadTexture("assets/textures/error.png");
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

Texture TextureLibrary::LoadTexture(const std::string& path)
{
    Texture texture;
    texture.Path = path;

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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture.width, texture.height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
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
    }

    return m_textures.at(path);
}

bool TextureLibrary::HasTexture(const std::string& path) const
{
    return m_textures.find(path) != m_textures.end();
}