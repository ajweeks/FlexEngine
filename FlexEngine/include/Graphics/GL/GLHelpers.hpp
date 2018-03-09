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

			u32 program;
		};

		struct GLCubemapGBuffer
		{
			u32 id;
			const char* name;
			GLint internalFormat;
			GLenum format;
		};

		struct GLMaterial
		{
			Renderer::Material material = {}; // More info is stored in the generic material struct

			struct UniformIDs
			{
				i32 model;
				i32 modelInvTranspose;
				i32 modelViewProjection;
				i32 viewProjection;
				i32 view;
				i32 viewInv;
				i32 projection;
				i32 camPos;
				i32 enableDiffuseTexture;
				i32 enableNormalTexture;
				i32 enableCubemapTexture;
				i32 constAlbedo;
				i32 enableAlbedoSampler;
				i32 constMetallic;
				i32 enableMetallicSampler;
				i32 constRoughness;
				i32 enableRoughnessSampler;
				i32 constAO;
				i32 enableAOSampler;
				i32 hdrEquirectangularSampler;
				i32 enableIrradianceSampler;
				i32 verticalScale;
			};
			UniformIDs uniformIDs;

			u32 diffuseSamplerID;
			u32 normalSamplerID;

			u32 cubemapSamplerID;
			std::vector<GLCubemapGBuffer> cubemapSamplerGBuffersIDs;
			u32 cubemapDepthSamplerID;

			// PBR samplers
			u32 albedoSamplerID;
			u32 metallicSamplerID;
			u32 roughnessSamplerID;
			u32 aoSamplerID;

			u32 hdrTextureID;

			u32 irradianceSamplerID;
			u32 prefilteredMapSamplerID;
			u32 brdfLUTSamplerID;
		};

		struct GLRenderObject
		{
			RenderID renderID;

			std::string name;
			std::string materialName;
			Transform* transform = nullptr;

			bool visible = true;
			bool isStatic = true; // If true, this object will be rendered to reflection probes
			bool visibleInSceneExplorer = true;

			u32 VAO;
			u32 VBO;
			u32 IBO;

			GLenum topology = GL_TRIANGLES;
			GLenum cullFace = GL_BACK;
			GLboolean enableCulling = GL_TRUE;

			GLenum depthTestReadFunc = GL_LEQUAL;
			GLboolean depthWriteEnable = GL_TRUE;

			u32 vertexBuffer;
			VertexBufferData* vertexBufferData = nullptr;

			bool indexed = false;
			u32 indexBuffer;
			std::vector<u32>* indices = nullptr;

			u32 materialID;
		};

		struct UniformInfo
		{
			const GLchar* name;
			i32* id;
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

		bool GenerateGLTexture_Empty(u32& textureID, const glm::vec2i& dimensions, bool generateMipMaps, GLenum i32ernalFormat, GLenum format, GLenum type);
		bool GenerateGLTexture_EmptyWithParams(u32& textureID, const glm::vec2i& dimensions, bool generateMipMaps, GLenum i32ernalFormat, GLenum format, GLenum type, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter);
		bool GenerateGLTexture(u32& textureID, const std::string& filePath, bool flipVertically, bool generateMipMaps);
		bool GenerateGLTextureWithParams(u32& textureID, const std::string& filePath, bool flipVertically, bool generateMipMaps, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter);
		bool GenerateHDRGLTexture(u32& textureID, const std::string& filePath, bool flipVertically, bool generateMipMaps);
		bool GenerateHDRGLTextureWithParams(u32& textureID, const std::string& filePath, bool flipVertically, bool generateMipMaps, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter);

		struct GLCubemapCreateInfo
		{
			u32 program = 0;
			u32* textureID = nullptr;
			u32* depthTextureID = nullptr;
			std::vector<GLCubemapGBuffer>* textureGBufferIDs = nullptr;
			glm::uvec2 textureSize;
			std::array<std::string, 6> filePaths; // Leave empty to generate an "empty" cubemap (no pixel data)
			bool generateMipmaps = false;
			bool enableTrilinearFiltering = false;
			bool HDR = false;
			bool generateDepthBuffers = false;
		};

		bool GenerateGLCubemap(GLCubemapCreateInfo& createInfo);

		bool LoadGLShaders(u32 program, GLShader& shader);
		bool LinkProgram(u32 program);


		GLboolean BoolToGLBoolean(bool value);
		GLuint BufferTargetToGLTarget(Renderer::BufferTarget bufferTarget);
		GLenum TypeToGLType(Renderer::Type type);
		GLenum UsageFlagToGLUsageFlag(Renderer::UsageFlag usage);
		GLenum TopologyModeToGLMode(Renderer::TopologyMode topology);
		u32 CullFaceToGLMode(Renderer::CullFace cullFace);
		GLenum DepthTestFuncToGlenum(Renderer::DepthTestFunc func);

	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL