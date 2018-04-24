#pragma once
#if COMPILE_OPEN_GL

#include <string>
#include <array>

#pragma warning(push, 0)
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#pragma warning(pop)

#include "Logger.hpp"
#include "Types.hpp"


namespace flex
{
	namespace gl
	{
#define InvalidID u32_max

		struct GLShader
		{
			GLShader(const std::string& name,
				const std::string& vertexShaderFilePath,
				const std::string& fragmentShaderFilePath);

			Shader shader;

			u32 program = 0;
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
			Material material = {}; // More info is stored in the generic material struct

			struct UniformIDs
			{
				i32 model;
				i32 modelInvTranspose;
				i32 modelViewProjection;
				i32 colorMultiplier;
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

			u32 diffuseSamplerID = 0;
			u32 normalSamplerID = 0;

			u32 cubemapSamplerID = 0;
			std::vector<GLCubemapGBuffer> cubemapSamplerGBuffersIDs;
			u32 cubemapDepthSamplerID = 0;

			// PBR samplers
			u32 albedoSamplerID = 0;
			u32 metallicSamplerID = 0;
			u32 roughnessSamplerID = 0;
			u32 aoSamplerID = 0;

			u32 hdrTextureID = 0;

			u32 irradianceSamplerID = 0;
			u32 prefilteredMapSamplerID = 0;
			u32 brdfLUTSamplerID = 0;
		};

		struct GLRenderObject
		{
			RenderID renderID = InvalidRenderID;

			std::string name = "";
			std::string materialName = "";
			Transform* transform = nullptr;

			bool visible = true;
			bool isStatic = true; // If true, this object will be rendered to reflection probes
			bool visibleInSceneExplorer = true;

			u32 VAO = 0;
			u32 VBO = 0;
			u32 IBO = 0;

			GLenum topology = GL_TRIANGLES;
			GLenum cullFace = GL_BACK;
			GLboolean enableCulling = GL_TRUE;

			GLenum depthTestReadFunc = GL_LEQUAL;
			GLboolean depthWriteEnable = GL_TRUE;

			u32 vertexBuffer = 0;
			VertexBufferData* vertexBufferData = nullptr;

			bool indexed = false;
			u32 indexBuffer = 0;
			std::vector<u32>* indices = nullptr;

			MaterialID materialID = InvalidMaterialID;
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
			glm::uvec2 textureSize = { 0, 0 };
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
		GLuint BufferTargetToGLTarget(BufferTarget bufferTarget);
		GLenum DataTypeToGLType(DataType dataType);
		GLenum UsageFlagToGLUsageFlag(UsageFlag usage);
		GLenum TopologyModeToGLMode(TopologyMode topology);
		u32 CullFaceToGLCullFace(CullFace cullFace);
		GLenum DepthTestFuncToGlenum(DepthTestFunc func);

		bool GLBooleanToBool(GLboolean boolean);
		BufferTarget GLTargetToBufferTarget(GLuint target);
		DataType GLTypeToDataType(GLenum type);
		UsageFlag GLUsageFlagToUsageFlag(GLenum usage);
		TopologyMode GLModeToTopologyMode(GLenum mode);
		CullFace GLCullFaceToCullFace(u32 cullFace);
		DepthTestFunc GlenumToDepthTestFunc(GLenum depthTestFunc);

	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL