#pragma once

struct aiScene;

class Mesh
{
public:
	Mesh();
	~Mesh();

	bool LoadFromFile(const std::string& filepath);

private:
	void SaveDataFromAIScene(const aiScene const* pScene);

	std::vector<VertexPosCol> m_Vertices;

};