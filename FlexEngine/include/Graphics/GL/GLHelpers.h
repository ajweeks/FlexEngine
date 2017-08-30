#pragma once
#if COMPILE_OPEN_GL

#include <string>
#include <array>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <SOIL.h>

#include "Logger.h"

namespace flex
{
	namespace gl
	{
		struct Material
		{
			std::string name;

			glm::uint shaderIndex;

			struct UniformIDs
			{
				int modelID;
				int modelInvTranspose;
				int modelViewProjection;
				int camPos;
				int viewDir;
				int ambientColor;
				int specularColor;
				int useDiffuseTexture;
				int useNormalTexture;
				int useSpecularTexture;
				int useCubemapTexture;
			};
			UniformIDs uniformIDs;

			bool useDiffuseTexture = false;
			std::string diffuseTexturePath;
			glm::uint diffuseTextureID;

			bool useSpecularTexture = false;
			std::string specularTexturePath;
			glm::uint specularTextureID;

			bool useNormalTexture = false;
			std::string normalTexturePath;
			glm::uint normalTextureID;

			std::array<std::string, 6> cubeMapFilePaths; // RT, LF, UP, DN, BK, FT
			bool useCubemapTexture = false;

			//Renderer::DirectionalLight directionalLight;
			//std::vector<Renderer::PointLight> pointLights;
		};

		struct RenderObject
		{
			RenderObject(RenderID renderID, std::string name = "");

			RenderID renderID;

			Renderer::RenderObjectInfo info;

			glm::uint VAO;
			glm::uint VBO;
			glm::uint IBO;

			GLenum topology = GL_TRIANGLES;
			GLenum cullFace = GL_BACK;

			glm::uint vertexBuffer;
			VertexBufferData* vertexBufferData = nullptr;

			bool indexed = false;
			glm::uint indexBuffer;
			std::vector<glm::uint>* indices = nullptr;

			glm::mat4 model;

			glm::uint materialID;
		};
		typedef std::vector<RenderObject*>::iterator RenderObjectIter;

		struct UniformInfo
		{
			Renderer::Uniform::Type type;
			int* id;
			const GLchar* name;
		};

		struct ViewProjectionUBO
		{
			glm::mat4 view;
			glm::mat4 proj;
		};

		struct ViewProjectionCombinedUBO
		{
			glm::mat4 viewProj;
		};

		struct Skybox
		{
			glm::uint textureID;
		};

		GLFWimage LoadGLFWimage(const std::string& filePath);
		void DestroyGLFWimage(const GLFWimage& image);

		void GenerateGLTexture(glm::uint& textureID, const std::string& filePath,
			int sWrap = GL_REPEAT, int tWrap = GL_REPEAT, int minFilter = GL_LINEAR, int magFilter = GL_LINEAR);

		void GenerateCubemapTextures(glm::uint& textureID, const std::array<std::string, 6> filePaths);

		bool LoadGLShaders(glm::uint program, const std::string& vertexShaderFilePath, const std::string& fragmentShaderFilePath);
		void LinkProgram(glm::uint program);


		GLuint BufferTargetToGLTarget(Renderer::BufferTarget bufferTarget);
		GLenum TypeToGLType(Renderer::Type type);
		GLenum UsageFlagToGLUsageFlag(Renderer::UsageFlag usage);
		GLenum TopologyModeToGLMode(Renderer::TopologyMode topology);
		glm::uint CullFaceToGLMode(Renderer::CullFace cullFace);
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL