#include "ModelLibrary.hpp"
#include "FenrirLogger/ILogger.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glad/glad.h>

Mesh::Mesh(std::vector<Vertex> verts, std::vector<unsigned int> ind, std::vector<Texture> texts)
    : vertices(std::move(verts)), indices(std::move(ind)), textures(std::move(texts))
{
}

// TODO move this to speicfic code for renderer
static void SetupMesh(Mesh& mesh)
{
    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);
    glGenBuffers(1, &mesh.EBO);

    glBindVertexArray(mesh.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), &mesh.vertices[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), &mesh.indices[0], GL_STATIC_DRAW);

    // vertex positions
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    // vertex normals
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    // vertex texture coords
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, texCoords)));

    glBindVertexArray(0);
}

ModelLibrary::ModelLibrary(Fenrir::ILogger& logger, TextureLibrary& textureLibrary, ShaderLibrary& shaderLibrary)
    : m_logger(logger), m_textureLibrary(textureLibrary), m_shaderLibrary(shaderLibrary)
{
}

void ModelLibrary::AddModel(const std::string& path)
{
    if (m_models.count(path) > 0)
    {
        m_logger.Warn("ModelLibrary::AddModel - Model already loaded: {}", path);
        return;
    }

    Model model;
    LoadModel(path, model);

    m_models.emplace(path, std::move(model));
}

const Model& ModelLibrary::GetModel(const std::string& path)
{
    if (m_models.count(path) == 0)
    {
        m_logger.Error("ModelLibrary::GetModel - Model not loaded: {}", path);

        m_logger.Info("ModelLibrary::GetModel - Attempting to load model: {}", path);

        AddModel(path);
    }

    return m_models.at(path);
}

bool ModelLibrary::HasModel(const std::string& path) const
{
    return m_models.count(path) > 0;
}

void ModelLibrary::LoadModel(const std::string& path, Model& model)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        m_logger.Error("ModelLibrary::LoadModel - {}", importer.GetErrorString());
        return;
    }

    model.directory = path.substr(0, path.find_last_of('/'));

    ProcessNode(scene->mRootNode, scene, model);
    CalculateAABB(model);
}

void ModelLibrary::CalculateAABB(Model& model)
{
    if (model.meshes.empty())
        return;

    Fenrir::Math::Vec3 globalMin = model.meshes[0].boundingBox.min;
    Fenrir::Math::Vec3 globalMax = model.meshes[0].boundingBox.max;

    for (auto& mesh : model.meshes)
    {
        UpdateMeshAABB(mesh); // get the mesh's AABB

        globalMin.x = std::min(globalMin.x, mesh.boundingBox.min.x);
        globalMin.y = std::min(globalMin.y, mesh.boundingBox.min.y);
        globalMin.z = std::min(globalMin.z, mesh.boundingBox.min.z);

        globalMax.x = std::max(globalMax.x, mesh.boundingBox.max.x);
        globalMax.y = std::max(globalMax.y, mesh.boundingBox.max.y);
        globalMax.z = std::max(globalMax.z, mesh.boundingBox.max.z);
    }

    model.boundingBox = Fenrir::Math::AABB(globalMin, globalMax);
}
void ModelLibrary::UpdateMeshAABB(Mesh& mesh)
{
    if (mesh.vertices.empty())
        return;

    Fenrir::Math::Vec3 min = mesh.vertices[0].pos;
    Fenrir::Math::Vec3 max = min;

    for (const auto& vertex : mesh.vertices)
    {
        min.x = std::min(min.x, vertex.pos.x);
        min.y = std::min(min.y, vertex.pos.y);
        min.z = std::min(min.z, vertex.pos.z);

        max.x = std::max(max.x, vertex.pos.x);
        max.y = std::max(max.y, vertex.pos.y);
        max.z = std::max(max.z, vertex.pos.z);
    }

    mesh.boundingBox = Fenrir::Math::AABB(min, max);
}

void ModelLibrary::ProcessNode(aiNode* node, const aiScene* scene, Model& model)
{
    // process all the node's meshes (if any)
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

        model.meshes.push_back(ProcessMesh(mesh, scene, model.directory));
    }

    // then do the same for each of its children
    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        ProcessNode(node->mChildren[i], scene, model);
    }
}

Mesh ModelLibrary::ProcessMesh(aiMesh* mesh, const aiScene* scene, std::string& directory)
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex vertex;
        glm::vec3
            vector; // we declare a placeholder vector since assimp uses its own vector class that doesn't directly
                    // convert to glm's vec3 class so we transfer the data to this placeholder glm::vec3 first.
        // positions
        vector.x = mesh->mVertices[i].x;
        vector.y = mesh->mVertices[i].y;
        vector.z = mesh->mVertices[i].z;
        vertex.pos = vector;
        // normals
        if (mesh->HasNormals())
        {
            vector.x = mesh->mNormals[i].x;
            vector.y = mesh->mNormals[i].y;
            vector.z = mesh->mNormals[i].z;
            vertex.normal = vector;
        }
        // texture coordinates
        if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
        {
            glm::vec2 vec;
            // a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't
            // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
            vec.x = mesh->mTextureCoords[0][i].x;
            vec.y = mesh->mTextureCoords[0][i].y;
            vertex.texCoords = vec;
            // // tangent
            // vector.x = mesh->mTangents[i].x;
            // vector.y = mesh->mTangents[i].y;
            // vector.z = mesh->mTangents[i].z;
            // vertex.Tangent = vector;
            // // bitangent
            // vector.x = mesh->mBitangents[i].x;
            // vector.y = mesh->mBitangents[i].y;
            // vector.z = mesh->mBitangents[i].z;
            // vertex.Bitangent = vector;
        }
        else
            vertex.texCoords = glm::vec2(0.0f, 0.0f);

        vertices.push_back(vertex);
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        // retrieve all indices of the face and store them in the indices vector
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }

    if (mesh->mMaterialIndex >= 0)
    {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

        std::vector<Texture> diffuseMaps =
            LoadMaterialTextures(material, aiTextureType_DIFFUSE, TextureType::Diffuse, directory);
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());

        std::vector<Texture> specularMaps =
            LoadMaterialTextures(material, aiTextureType_SPECULAR, TextureType::Specular, directory);
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
    }

    Mesh returnMesh(vertices, indices, textures);

    SetupMesh(returnMesh);

    return returnMesh;
}

std::vector<Texture> ModelLibrary::LoadMaterialTextures(aiMaterial* mat, aiTextureType type, TextureType textureType,
                                                        const std::string& directory)
{
    std::vector<Texture> textures;

    for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
    {
        aiString str;
        mat->GetTexture(type, i, &str);

        std::string path = directory + "/" + str.C_Str();

        if (!m_textureLibrary.HasTexture(path))
        {
            m_textureLibrary.AddTexture(path);
        }

        textures.push_back(m_textureLibrary.GetTexture(path));
    }

    return textures;
}