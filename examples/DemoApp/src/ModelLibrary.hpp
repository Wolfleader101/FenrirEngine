#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "FenrirMath/Math.hpp"
#include "TextureLibrary.hpp"

struct aiNode;
struct aiScene;
struct aiMesh;
struct aiMaterial;
enum aiTextureType;

namespace Fenrir
{
    class ILogger;
}

class ShaderLibrary;

struct Vertex
{
    Fenrir::Math::Vec3 pos;
    Fenrir::Math::Vec3 normal;
    Fenrir::Math::Vec2 texCoords;
};

class Mesh
{
  public:
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    Mesh() = default;
    Mesh(std::vector<Vertex> verts, std::vector<unsigned int> ind, std::vector<Texture> texts);

    unsigned int VAO;
    unsigned int VBO;
    unsigned int EBO;
};

class Model
{
  public:
    std::vector<Mesh> meshes;

    std::string directory;
};

class ModelLibrary
{
  public:
    ModelLibrary(Fenrir::ILogger& logger, TextureLibrary& textureLibrary, ShaderLibrary& shaderLibrary);

    void AddModel(const std::string& path);

    const Model& GetModel(const std::string& path);

    bool HasModel(const std::string& path) const;

  private:
    std::unordered_map<std::string, Model> m_models;

    Fenrir::ILogger& m_logger;

    TextureLibrary& m_textureLibrary;
    ShaderLibrary& m_shaderLibrary;

    void LoadModel(const std::string& path, Model& model);

    void ProcessNode(aiNode* node, const aiScene* scene, Model& model);

    Mesh ProcessMesh(aiMesh* mesh, const aiScene* scene, std::string& directory);

    std::vector<Texture> LoadMaterialTextures(aiMaterial* mat, aiTextureType type, TextureType textureType,
                                              const std::string& directory);
};
