#pragma once
#if COMPILE_OPEN_GL

#include <array>
#include <thread>
#include <future>

#pragma warning(push, 0)
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#pragma warning(pop)

#include "Graphics/RendererTypes.hpp"
#include "Helpers.hpp"

namespace flex
{
	namespace gl
	{
#define InvalidID u32_max

		struct GLShader
		{
			GLShader(const std::string& name,
					 const std::string& vertexShaderFilePath,
					 const std::string& fragmentShaderFilePath,
					 const std::string& geometryShaderFilePath = "");

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
				i32 colorMultiplier;
				i32 lightViewProjection;
				i32 exposure;
				i32 viewProjection;
				i32 view;
				i32 viewInv;
				i32 projection;
				i32 camPos;
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
				i32 transformMat;
				i32 texSize;
				i32 castShadows;
				i32 shadowDarkness;
				i32 textureScale;
			};
			UniformIDs uniformIDs;

			u32 normalSamplerID = InvalidID;

			u32 cubemapSamplerID = InvalidID;
			std::vector<GLCubemapGBuffer> cubemapSamplerGBuffersIDs;
			u32 cubemapDepthSamplerID = InvalidID;

			// PBR samplers
			u32 albedoSamplerID = InvalidID;
			u32 metallicSamplerID = InvalidID;
			u32 roughnessSamplerID = InvalidID;
			u32 aoSamplerID = InvalidID;

			u32 hdrTextureID = InvalidID;

			u32 irradianceSamplerID = InvalidID;
			u32 prefilteredMapSamplerID = InvalidID;
			u32 brdfLUTSamplerID = InvalidID;
		};

		struct GLRenderObject
		{
			RenderID renderID = InvalidRenderID;

			GameObject* gameObject = nullptr;

			std::string materialName = "";

			u32 VAO = InvalidID;
			u32 VBO = InvalidID;
			u32 IBO = InvalidID;

			GLenum topology = GL_TRIANGLES;
			GLenum cullFace = GL_BACK;
			GLboolean enableCulling = GL_TRUE;

			// TODO: Remove these in place of DrawCallInfo members
			GLenum depthTestReadFunc = GL_LEQUAL;
			GLboolean depthWriteEnable = GL_TRUE;

			u32 vertexBuffer = 0;
			VertexBufferData* vertexBufferData = nullptr;

			bool indexed = false;
			u32 indexBuffer = 0;
			std::vector<u32>* indices = nullptr;

			// If true this object will be drawn after post processing
			// and not drawn in shipping builds
			bool editorObject = false;

			MaterialID materialID = InvalidMaterialID;
		};

		struct UniformInfo
		{
			Uniform uniform;
			const char* name;
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

		struct ImageInfo
		{
			i32 width;
			i32 height;
			i32 channelCount;
		};

		bool GenerateGLTexture_Empty(u32& textureID, const glm::vec2i& dimensions, bool generateMipMaps, GLenum internalFormat, GLenum format, GLenum type);
		bool GenerateGLTexture_EmptyWithParams(u32& textureID, const glm::vec2i& dimensions, bool generateMipMaps, GLenum internalFormat, GLenum format, GLenum type, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter);
		bool GenerateGLTexture(u32& textureID, const std::string& filePath, i32 requestedChannelCount, bool flipVertically, bool generateMipMaps, ImageInfo* infoOut = nullptr);
		bool GenerateGLTextureWithParams(u32& textureID, const std::string& filePath, i32 requestedChannelCount, bool flipVertically, bool generateMipMaps, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter, ImageInfo* infoOut = nullptr);
		bool GenerateHDRGLTexture(u32& textureID, const std::string& filePath, i32 requestedChannelCount, bool flipVertically, bool generateMipMaps, ImageInfo* infoOut = nullptr);
		bool GenerateHDRGLTextureWithParams(u32& textureID, const std::string& filePath, i32 requestedChannelCount, bool flipVertically, bool generateMipMaps, i32 sWrap, i32 tWrap, i32 minFilter, i32 magFilter, ImageInfo* infoOut = nullptr);

		struct GLCubemapCreateInfo
		{
			u32 program = 0;
			u32* textureID = nullptr;
			u32* depthTextureID = nullptr;
			std::vector<GLCubemapGBuffer>* textureGBufferIDs = nullptr;
			glm::vec2 textureSize = { 0, 0 };
			std::array<std::string, 6> filePaths; // Leave empty to generate an "empty" cubemap (no pixel data)
			bool generateMipmaps = false;
			bool enableTrilinearFiltering = false;
			bool HDR = false;
			bool generateDepthBuffers = false;
		};

		bool GenerateGLCubemap(GLCubemapCreateInfo& createInfo);

		struct TextureParameters
		{
			TextureParameters(bool bGenMipMaps = false, bool bDepthTex = false);

			//Parameters
			i32 minFilter = GL_LINEAR;
			i32 magFilter = GL_LINEAR;
			i32 wrapS = GL_REPEAT;
			i32 wrapT = GL_REPEAT;
			i32 wrapR = GL_REPEAT;
			glm::vec4 borderColor;

			bool bGenMipMaps = false;
			bool bDepthTex = false;

			i32 compareMode = GL_COMPARE_REF_TO_TEXTURE;
		};

		struct GLTexture
		{
			GLTexture();
			GLTexture(const std::string& name, i32 width, i32 height, i32 channelCount, i32 internalFormat, GLenum format, GLenum type);
			GLTexture(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR);
			~GLTexture();

			bool GenerateEmpty();
			bool LoadFromFile();

			void Reload();

			void Build(void* data = nullptr);
			void Destroy();
			void SetParameters(TextureParameters params);

			glm::vec2i GetResolution();

			// Returns true if regenerated
			// If this is a framebuffer texture, upscaling won't work properly
			// unless it is reattached to the framebuffer object
			bool Resize(glm::vec2i newSize);

			std::string GetRelativeFilePath() const;
			std::string GetName() const;

			bool SaveToFile(const std::string& absoluteFilePath, ImageFormat format, bool bFlipVertically);
			// TODO: Add AsyncSave member func

		private:
			std::string relativeFilePath;
			std::string name; // absFilePath but without the leading directories, or a custom name if not loaded from file

		public:

			GLuint handle = 0;

			i32 width = 0;
			i32 height = 0;

			bool bHasMipMaps = false;
			bool bFlipVerticallyOnLoad = false;
			bool bHDR = false;
			i32 channelCount = 0;

			bool bLoaded = false;

			i32 internalFormat = GL_RGB;
			GLenum format = GL_RGB;
			GLenum type = GL_FLOAT;

			TextureParameters m_Parameters;
		};

		struct AsynchronousTextureSave
		{
			AsynchronousTextureSave(const std::string& absoluteFilePath, ImageFormat format, GLuint handle, i32 width, i32 height, i32 channelCount, bool bFlipVertically, u8* srcData, i32 numBytes);
			~AsynchronousTextureSave();

			// Returns true once task is complete
			bool TickStatus();

			std::thread taskThread;
			std::promise<bool> taskPromise;
			std::future<bool> taskFuture;
			std::string absoluteFilePath;

			GLuint textureID = 0;
			u8* data = nullptr;

			bool bSuccess = false;
			bool bComplete = false;

			sec totalSecWaiting = 0.0f;
			sec secBetweenStatusChecks = 0.05f;
			sec secSinceStatusCheck = 0.0f;
		};

		void StartAsyncTextureSaveToFile(const std::string& absoluteFilePath, ImageFormat format, GLuint handle, i32 width, i32 height, i32 channelCount, bool bFlipVertically, AsynchronousTextureSave** asyncTextureSave);
		bool SaveTextureToFile(const std::string& absoluteFilePath, ImageFormat format, GLuint handle, i32 width, i32 height, i32 channelCount, bool bFlipVertically);

		bool LoadGLShaders(u32 program, GLShader& shader);
		bool LinkProgram(u32 program);
		bool IsProgramValid(u32 program);

		void PrintProgramInfoLog(u32 program);
		void PrintShaderInfo(u32 program, const char* shaderName = nullptr);

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