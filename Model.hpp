#ifndef MODEL_H
#define MODEL_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <string>
#include <iostream>
#include <vector>

#include "Mesh.hpp"

class Model
{
public:
    //multiple sub-meshes
    std::vector<Mesh> meshes;

    // Constructor loads the file
    Model(const std::string& path)
    {
        loadModel(path);
    }

    // Draw all sub-meshes 
    void Draw(GLuint programID)
    {

        GLint materialLoc = glGetUniformLocation(programID, "uMaterialID");
        GLint timeLoc = glGetUniformLocation(programID, "iTime");


        float currentTime = (float)glfwGetTime(); 

        for (auto& m : meshes)
        {
            // Set the uniform with the mesh's material ID
            glUniform1i(materialLoc, m.materialID);

    
            glUniform1f(timeLoc, currentTime);

            // Draw the mesh
            m.Draw(programID);
        }
    }

private:
    //Assimp to read the file
    void loadModel(const std::string& path)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(
            path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs |
            aiProcess_JoinIdenticalVertices
        );

        if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
        {
            std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
            return;
        }

        // process root node
        processNode(scene->mRootNode, scene);
    }

    void processNode(aiNode* node, const aiScene* scene)
    {
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* aimesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(aimesh, scene));
        }
        // Recursively
        for (unsigned int c = 0; c < node->mNumChildren; c++)
        {
            processNode(node->mChildren[c], scene);
        }
    }

    Mesh processMesh(aiMesh* mesh, const aiScene* scene)
    {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        // 1) Fill vertices
        vertices.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex v;
            // Positions
            v.Position = glm::vec3(
                mesh->mVertices[i].x,
                mesh->mVertices[i].y,
                mesh->mVertices[i].z
            );
            // Normals
            if (mesh->HasNormals())
            {
                v.Normal = glm::vec3(
                    mesh->mNormals[i].x,
                    mesh->mNormals[i].y,
                    mesh->mNormals[i].z
                );
            }
            // TexCoords
            if (mesh->mTextureCoords[0])
            {
                v.TexCoords = glm::vec2(
                    mesh->mTextureCoords[0][i].x,
                    mesh->mTextureCoords[0][i].y
                );
            }
            else
            {
                v.TexCoords = glm::vec2(0.0f, 0.0f);
            }
            vertices.push_back(v);
        }

        
        for (unsigned int f = 0; f < mesh->mNumFaces; f++)
        {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
            {
                indices.push_back(face.mIndices[j]);
            }
        }

      
        int materialID = 1;  // default value 

        unsigned int matIndex = mesh->mMaterialIndex;
        aiMaterial* aiMat = scene->mMaterials[matIndex];

        aiString aiMatName;
        aiMat->Get(AI_MATKEY_NAME, aiMatName);
        std::string matName = aiMatName.C_Str();

        //Just for debug
        std::cout << "Mesh name: " << mesh->mName.C_Str()
            << " / Material name: " << matName << std::endl;

        // Decide the ID
        if (matName.find("Bianco") != std::string::npos)
        {
            materialID = 1;
        }
        else if (matName.find("Nero") != std::string::npos)
        {
            materialID = 2;
        }
        else if (matName.find("Legno") != std::string::npos)
        {
            materialID = 3;
        }
        else if (matName.find("CaselleBianche") != std::string::npos)
        {
            materialID = 4;
        }
        else if (matName.find("CaselleNere") != std::string::npos)
        {
            materialID = 5;
        }
       

        // Construct the mesh
        Mesh newMesh(vertices, indices);
        newMesh.materialID = materialID;  // <--- store the ID in the mesh
        return newMesh;
    }
};

#endif
