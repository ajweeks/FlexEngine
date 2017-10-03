#pragma once
#if COMPILE_OPEN_GL

#include <string>
#include <array>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Logger.h"

namespace flex
{
	namespace gl
	{
		struct GLMaterial
		{
			Renderer::Material material; // More info is stored in the generic material struct

			struct UniformIDs
			{
				int model;
				int modelInvTranspose;
				int modelViewProjection;
				int viewProjection;
				int view;
				int viewInv;
				int projection;
				int camPos;
				int useDiffuseTexture;
				int useNormalTexture;
				int useSpecularTexture;
				int useCubemapTexture;
				int constAlbedo;
				int useAlbedoSampler;
				int constMetallic;
				int useMetallicSampler;
				int constRoughness;
				int useRoughnessSampler;
				int constAO;
				int useAOSampler;
				int equirectangularSampler;
			};
			UniformIDs uniformIDs;

			glm::uint diffuseSamplerID;
			glm::uint specularSamplerID;
			glm::uint normalSamplerID;

			glm::uint cubemapSamplerID;

			// GBuffer samplers
			glm::uint positionFrameBufferSamplerID;
			glm::uint normalFrameBufferSamplerID;
			glm::uint diffuseSpecularFrameBufferSamplerID;
			
			// PBR samplers
			glm::uint albedoSamplerID;
			glm::uint metallicSamplerID;
			glm::uint roughnessSamplerID;
			glm::uint aoSamplerID;

			glm::uint hdrTextureID;
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
			const GLchar* name;
			int* id;
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

		bool GenerateGLTexture(glm::uint& textureID, const std::string& filePath);
		bool GenerateGLTextureWithParams(glm::uint& textureID, const std::string& filePath, int sWrap, int tWrap, int minFilter, int magFilter);
		bool GenerateHDRGLTexture(glm::uint& textureID, const std::string& filePath);
		bool GenerateHDRGLTextureWithParams(glm::uint& textureID, const std::string& filePath, int sWrap, int tWrap, int minFilter, int magFilter);

		bool GenerateGLCubemapTextures(glm::uint& textureID, const std::array<std::string, 6> filePaths);
		bool GenerateGLCubemap_Empty(glm::uint& textureID, int textureWidth, int textureHeight);

		bool LoadGLShaders(glm::uint program, Renderer::Shader& shader);
		bool LinkProgram(glm::uint program);


		GLuint BufferTargetToGLTarget(Renderer::BufferTarget bufferTarget);
		GLenum TypeToGLType(Renderer::Type type);
		GLenum UsageFlagToGLUsageFlag(Renderer::UsageFlag usage);
		GLenum TopologyModeToGLMode(Renderer::TopologyMode topology);
		glm::uint CullFaceToGLMode(Renderer::CullFace cullFace);
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL