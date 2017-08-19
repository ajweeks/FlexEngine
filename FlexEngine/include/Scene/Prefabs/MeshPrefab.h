#pragma once

#include "Scene\GameObject.h"

#include <vector>

#include <glm\integer.hpp>
#include <glm\vec2.hpp>
#include <glm\vec3.hpp>
#include <glm\vec4.hpp>

#include <assimp\scene.h>

#include "Transform.h"
#include "VertexBufferData.h"

namespace flex
{
	class MeshPrefab : public GameObject
	{
	public:
		MeshPrefab();
		~MeshPrefab();

		enum class PrefabShape
		{
			CUBE,
			UV_SPHERE,
			GRID,
			SKYBOX
		};

		bool LoadFromFile(const GameContext& gameContext, const std::string& filepath);
		bool LoadPrefabShape(const GameContext& gameContext, PrefabShape shape);

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;
		virtual void Destroy(const GameContext& gameContext) override;

		void SetUsedTextures(bool useDiffuse, bool useNormal, bool useSpecular);
		void SetShaderIndex(glm::uint shaderIndex);

		void SetTransform(const Transform& transform);
		Transform& GetTransform();

	private:
		glm::uint CalculateVertexBufferStride() const;
		void CreateVertexBuffer(VertexBufferData* vertexBufferData);
		void DescribeShaderVariables(const GameContext& gameContext, VertexBufferData* vertexBufferData);

		Transform m_Transform;
		RenderID m_RenderID;

		glm::uint m_ShaderIndex = 0;
		bool m_UseDiffuse = true;
		bool m_UseNormal = true;
		bool m_UseSpecular = true;

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
} // namespace flex
