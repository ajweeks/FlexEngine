#pragma once

#include "Scene/GameObject.h"
#include "Transform.h"
#include "GameContext.h"
#include "VertexBufferData.h"

#include <vector>

struct aiScene;

class MeshPrefab : public GameObject
{
public:
	MeshPrefab();
	~MeshPrefab();

	enum class PrefabShape
	{
		CUBE,
		UV_SPHERE,
		GRID
	};

	bool LoadFromFile(const GameContext& gameContext, const std::string& filepath);
	bool LoadPrefabShape(const GameContext& gameContext, PrefabShape shape);

	virtual void Initialize(const GameContext& gameContext) override;
	virtual void UpdateAndRender(const GameContext& gameContext) override;
	virtual void Destroy(const GameContext& gameContext) override;

	void SetTransform(const Transform& transform);
	Transform& GetTransform();

private:
	glm::uint CalculateVertexBufferStride() const;
	void CreateVertexBuffer(VertexBufferData* vertexBufferData);
	void DescribeShaderVariables(const GameContext& gameContext, glm::uint program, VertexBufferData* vertexBufferData);

	Transform m_Transform;
	glm::uint m_RenderID;

	std::string m_Name;

	std::vector<VertexBufferData> m_VertexBuffers;

	glm::uint m_Attributes;
	std::vector<glm::vec3> m_Positions;
	std::vector<glm::vec4> m_Colors;
	std::vector<glm::vec3> m_Tangents;
	std::vector<glm::vec3> m_Bitangents;
	std::vector<glm::vec3> m_Normals;
	std::vector<glm::vec2> m_TexCoords;

	std::vector<glm::uint> m_Indices;

	static glm::vec4 m_DefaultColor;
	static glm::vec3 m_DefaultPosition;
	static glm::vec3 m_DefaultTangent;
	static glm::vec3 m_DefaultBitangent;
	static glm::vec3 m_DefaultNormal;
	static glm::vec2 m_DefaultTexCoord;

};