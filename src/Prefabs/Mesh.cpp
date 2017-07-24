#include "stdafx.h"

#include "Prefabs/Mesh.h"

#include "Logger.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

Mesh::Mesh()
{
}

Mesh::~Mesh()
{
}

bool Mesh::LoadFromFile(const std::string& filepath)
{
	Assimp::Importer importer;

	const aiScene* scene = importer.ReadFile(filepath,
		aiProcess_CalcTangentSpace |
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_SortByPType);

	if (!scene)
	{
		Logger::LogError(importer.GetErrorString());
		return false;
	}

	SaveDataFromAIScene(scene);

	return true;
}

void Mesh::SaveDataFromAIScene(const aiScene const* pScene)
{
	Logger::Assert(pScene->HasMeshes());
	pScene->mMeshes[0]->mNumVertices;

}
