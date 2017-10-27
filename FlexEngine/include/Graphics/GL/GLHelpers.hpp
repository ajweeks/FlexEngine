#pragma once
#if COMPILE_OPEN_GL

#include <string>
#include <array>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Logger.hpp"

namespace flex
{
	namespace gl
	{
		struct GLShader
		{
			Renderer::Shader shader = {};

			glm::uint program;
		};

		struct GLMaterial
		{
			Renderer::Material material = {}; // More info is stored in the generic material struct

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
				int enableDiffuseTexture;
				int enableNormalTexture;
				int enableSpecularTexture;
				int enableCubemapTexture;
				int constAlbedo;
				int enableAlbedoSampler;
				int constMetallic;
				int enableMetallicSampler;
				int constRoughness;
				int enableRoughnessSampler;
				int constAO;
				int enableAOSampler;
				int hdrEquirectangularSampler;
				int enableIrradianceSampler;
			};
			UniformIDs uniformIDs;

			glm::uint diffuseSamplerID;
			glm::uint specularSamplerID;
			glm::uint normalSamplerID;

			glm::uint cubemapSamplerID;
			glm::uint cubemapDepthSamplerID;

			// PBR samplers
			glm::uint albedoSamplerID;
			glm::uint metallicSamplerID;
			glm::uint roughnessSamplerID;
			glm::uint aoSamplerID;

			glm::uint hdrTextureID;

			glm::uint irradianceSamplerID;
			glm::uint prefilteredMapSamplerID;
			glm::uint brdfLUTSamplerID;
		};

		struct GLRenderObject
		{
			RenderID renderID;

			std::string name;
			std::string materialName;
			Transform* transform = nullptr;

			bool visible = true;
			bool isStatic = true; // If true, this object will be rendered to reflection probes

			glm::uint VAO;
			glm::uint VBO;
			glm::uint IBO;

			GLenum topology = GL_TRIANGLES;
			GLenum cullFace = GL_BACK;

			GLenum depthTestReadFunc = GL_LEQUAL;
			GLboolean depthWriteEnable = GL_TRUE;

			glm::uint vertexBuffer;
			VertexBufferData* vertexBufferData = nullptr;

			bool indexed = false;
			glm::uint indexBuffer;
			std::vector<glm::uint>* indices = nullptr;

			glm::uint materialID;
		};
		typedef std::vector<GLRenderObject*>::iterator RenderObjectIter;

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

		bool GenerateGLTexture_Empty(glm::uint& textureID, glm::vec2i dimensions, bool generateMipMaps, GLenum internalFormat, GLenum format, GLenum type);
		bool GenerateGLTexture_EmptyWithParams(glm::uint& textureID, glm::vec2i dimensions, bool generateMipMaps, GLenum internalFormat, GLenum format, GLenum type, int sWrap, int tWrap, int minFilter, int magFilter);
		bool GenerateGLTexture(glm::uint& textureID, const std::string& filePath, bool generateMipMaps);
		bool GenerateGLTextureWithParams(glm::uint& textureID, const std::string& filePath, bool generateMipMaps, int sWrap, int tWrap, int minFilter, int magFilter);
		bool GenerateHDRGLTexture(glm::uint& textureID, const std::string& filePath, bool generateMipMaps);
		bool GenerateHDRGLTextureWithParams(glm::uint& textureID, const std::string& filePath, bool generateMipMaps, int sWrap, int tWrap, int minFilter, int magFilter);

		struct GLCubemapCreateInfo
		{
			glm::uint* textureID;
			glm::uint* depthTextureID;
			int textureWidth;
			std::array<std::string, 6> filePaths; // Leave empty to generate an "empty" cubemap (no pixel data)
			int textureHeight;
			bool generateMipmaps = false;
			bool enableTrilinearFiltering = false;
			bool HDR = false;
			bool generateDepthBuffers = false;
		};

		bool GenerateGLCubemap(GLCubemapCreateInfo& createInfo);

		bool LoadGLShaders(glm::uint program, GLShader& shader);
		bool LinkProgram(glm::uint program);


		GLboolean BoolToGLBoolean(bool value);
		GLuint BufferTargetToGLTarget(Renderer::BufferTarget bufferTarget);
		GLenum TypeToGLType(Renderer::Type type);
		GLenum UsageFlagToGLUsageFlag(Renderer::UsageFlag usage);
		GLenum TopologyModeToGLMode(Renderer::TopologyMode topology);
		glm::uint CullFaceToGLMode(Renderer::CullFace cullFace);
		GLenum DepthTestFuncToGlenum(Renderer::DepthTestFunc func);

	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL