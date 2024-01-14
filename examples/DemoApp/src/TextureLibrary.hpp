#pragma once

#include <string>
#include <unordered_map>
#include <vector>
namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

enum class TextureType
{
    Diffuse,
    Specular,
    Normal,
    Height
};

class Texture
{
  public:
    std::string path;
    unsigned int Id;
    TextureType type;
    int width;
    int height;
    int nrChannels;
};

class Skybox
{
  public:
    unsigned int Id;
    std::string name = "";

    std::string top = "";
    std::string bottom = "";
    std::string left = "";
    std::string right = "";
    std::string front = "";
    std::string back = "";
};

class TextureLibrary
{
  public:
    /**
     * @brief Construct a new Texture Library object
     *
     * @param logger to inject
     */
    TextureLibrary(Fenrir::ILogger& logger);

    /**
     * @brief Add a texture to the texture library
     *
     * @param path to the texture
     */
    void AddTexture(const std::string& path);

    /**
     * @brief Get the Texture object
     *
     * @param path to the texture
     * @return Texture& reference to the texture
     */
    const Texture& GetTexture(const std::string& path);

    static void ImportTexture(const std::string& path, int& width, int& height, int& nrChannels, unsigned char*& data,
                              bool flipVertically);
    static void FreeTexture(unsigned char* data);

    /**
     * @brief Check if the texture is loaded
     *
     * @param path to the texture
     * @return true if the texture is loaded
     * @return false if the texture is not loaded
     */
    bool HasTexture(const std::string& path) const;

    void AddSkybox(const std::string& name, const std::string& top, const std::string& bottom, const std::string& left,
                   const std::string& right, const std::string& front, const std::string& back);

    const Skybox& GetSkybox(const std::string& name);

    bool HasSkybox(const std::string& name) const;

  private:
    Fenrir::ILogger& m_logger;

    std::unordered_map<std::string, Texture> m_textures;
    std::unordered_map<std::string, Skybox> m_skyboxes;

    /**
     * @brief Load a texture from a file
     *
     * @param path to the texture
     */
    Texture LoadTexture(const std::string& path);

    void LoadSkybox(Skybox& outSkybox);
};