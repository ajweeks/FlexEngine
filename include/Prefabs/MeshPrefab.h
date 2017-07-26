#pragma once

#include "Transform.h"
#include "GameContext.h"
#include "VertexBufferData.h"

#include <vector>

struct aiScene;

class MeshPrefab
{
public:
	MeshPrefab();
	~MeshPrefab();

	enum class PrefabShape
	{
		CUBE,
		ICO_SPHERE,
		UV_SPHERE,
		GRID
		// TORUS, TEAPOT, etc...
	};

	enum class ILSemantic : glm::uint
	{
		NONE = 0,
		POSITION = (1 << 0),
		NORMAL = (1 << 1),
		COLOR = (1 << 2),
		TEXCOORD = (1 << 3),
		BINORMAL = (1 << 4),
		TANGENT = (1 << 5),
	};

	bool LoadFromFile(const GameContext& gameContext, const std::string& filepath);
	bool LoadPrefabShape(const GameContext& gameContext, PrefabShape shape);
	
	virtual void Render(const GameContext& gameContext);
	virtual void Destroy(const GameContext& gameContext);

	void SetTransform(const Transform& transform);
	Transform& GetTransform();

private:
	// Calculate stride using m_HasElement
	glm::uint CalculateVertexBufferStride() const;
	void CreateVertexBuffer(VertexBufferData* vertexBufferData);

	Transform m_Transform;
	glm::uint m_RenderID;

	std::vector<VertexBufferData> m_VertexBuffers;

	std::vector<glm::uint> m_Indices;
	glm::uint m_HasElement;
	std::vector<glm::vec3> m_Positions;
	std::vector<glm::vec4> m_Colors;
	std::vector<glm::vec3> m_Normals;
	std::vector<glm::vec2> m_TexCoords;
	//std::vector<glm::vec3> m_Tangents;
	//std::vector<glm::vec3> m_Binormals;

	static glm::vec4 m_DefaultColor;
	static glm::vec3 m_DefaultPosition;
	static glm::vec3 m_DefaultNormal;
	static glm::vec2 m_DefaultTexCoord;

};