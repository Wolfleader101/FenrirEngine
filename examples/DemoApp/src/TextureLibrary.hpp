#pragma once

#include <string>
#include <unordered_map>
namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

class Texture
{
  public:
    std::string Path;
    unsigned int Id;

    int width;
    int height;
    int nrChannels;

    // int channels;

    //   private:
    //     std::string m_path;

    //     unsigned int m_id;
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

    /**
     * @brief Check if the texture is loaded
     *
     * @param path to the texture
     * @return true if the texture is loaded
     * @return false if the texture is not loaded
     */
    bool HasTexture(const std::string& path) const;

  private:
    Fenrir::ILogger& m_logger;

    std::unordered_map<std::string, Texture> m_textures;

    /**
     * @brief Load a texture from a file
     *
     * @param path to the texture
     */
    Texture LoadTexture(const std::string& path);
};