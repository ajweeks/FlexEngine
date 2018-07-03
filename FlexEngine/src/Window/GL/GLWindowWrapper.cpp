#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Window/GL/GLWindowWrapper.hpp"

#include <sstream>

#include "Graphics/GL/GLHelpers.hpp"
#include "Helpers.hpp"
#include "Logger.hpp"

namespace flex
{
	namespace gl
	{
		GLWindowWrapper::GLWindowWrapper(std::string title, GameContext& gameContext) :
			GLFWWindowWrapper(title, gameContext)
		{
		}

		GLWindowWrapper::~GLWindowWrapper()
		{
		}

		void GLWindowWrapper::Create(glm::vec2i size, glm::vec2i pos)
		{
			m_Size = size;
			m_FrameBufferSize = size;
			m_LastWindowedSize = size;
			m_Position = pos;
			m_StartingPosition = pos;
			m_LastWindowedPos = pos;

#if _DEBUG
			glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif // _DEBUG

			// Don't hide window when losing focus in Windowed Fullscreen
			glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

			m_Window = glfwCreateWindow(m_Size.x, m_Size.y, m_TitleString.c_str(), NULL, NULL);
			if (!m_Window)
			{
				Logger::LogError("Failed to create glfw Window! Exiting");
				glfwTerminate();
				// TODO: Try creating a window manually here
				exit(EXIT_FAILURE);
			}

			glfwSetWindowUserPointer(m_Window, this);

			SetUpCallbacks();

			glfwSetWindowPos(m_Window, m_StartingPosition.x, m_StartingPosition.y);

			glfwFocusWindow(m_Window);
			m_HasFocus = true;

			glfwMakeContextCurrent(m_Window);

			gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
			CheckGLErrorMessages();

#if _DEBUG
			if (glDebugMessageCallback)
			{
				glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
				glDebugMessageCallback(glDebugOutput, nullptr);
				GLuint unusedIds = 0;
				glDebugMessageControl(GL_DONT_CARE,
									  GL_DONT_CARE,
									  GL_DONT_CARE,
									  0,
									  &unusedIds,
									  true);
			}
#endif // _DEBUG

			Logger::LogInfo("OpenGL loaded");
			Logger::LogInfo("Vendor:\t\t" + std::string(reinterpret_cast<const char*>(glGetString(GL_VENDOR))));
			Logger::LogInfo("Renderer:\t" + std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))));
			Logger::LogInfo("Version:\t\t" + std::string(reinterpret_cast<const char*>(glGetString(GL_VERSION))) + "\n");
			CheckGLErrorMessages();

			if (!m_WindowIcons.empty() && m_WindowIcons[0].pixels)
			{
				glfwSetWindowIcon(m_Window, m_WindowIcons.size(), m_WindowIcons.data());
			}
		}

		void WINAPI glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
			const GLchar* message, const void* userParam)
		{
			UNREFERENCED_PARAMETER(userParam);
			UNREFERENCED_PARAMETER(length);

			// Ignore insignificant error/warning codes
			if (id == 131169 || id == 131185 || id == 131218 || id == 131204)
			{
				return;
			}

			std::stringstream ss;
			ss << "---------------" << std::endl << "\t";
			ss << "GL Debug message (" << id << "): " << message;

			switch (source)
			{
			case GL_DEBUG_SOURCE_API:             ss << "Source: API"; break;
			case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   ss << "Source: Window System"; break;
			case GL_DEBUG_SOURCE_SHADER_COMPILER: ss << "Source: Shader Compiler"; break;
			case GL_DEBUG_SOURCE_THIRD_PARTY:     ss << "Source: Third Party"; break;
			case GL_DEBUG_SOURCE_APPLICATION:     ss << "Source: Application"; break;
			case GL_DEBUG_SOURCE_OTHER:           ss << "Source: Other"; break;
			}
			ss << std::endl << "\t";

			switch (type)
			{
			case GL_DEBUG_TYPE_ERROR:               ss << "Type: Error"; break;
			case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: ss << "Type: Deprecated Behaviour"; break;
			case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  ss << "Type: Undefined Behaviour"; break;
			case GL_DEBUG_TYPE_PORTABILITY:         ss << "Type: Portability"; break;
			case GL_DEBUG_TYPE_PERFORMANCE:         ss << "Type: Performance"; break;
			case GL_DEBUG_TYPE_MARKER:              ss << "Type: Marker"; break;
			case GL_DEBUG_TYPE_PUSH_GROUP:          ss << "Type: Push Group"; break;
			case GL_DEBUG_TYPE_POP_GROUP:           ss << "Type: Pop Group"; break;
			case GL_DEBUG_TYPE_OTHER:               ss << "Type: Other"; break;
			}
			ss << std::endl << "\t";

			switch (severity)
			{
			case GL_DEBUG_SEVERITY_HIGH:         ss << "Severity: high"; break;
			case GL_DEBUG_SEVERITY_MEDIUM:       ss << "Severity: medium"; break;
			case GL_DEBUG_SEVERITY_LOW:          ss << "Severity: low"; break;
			case GL_DEBUG_SEVERITY_NOTIFICATION: ss << "Severity: notification"; break;
			}

			Logger::LogError(ss.str());
			Logger::LogError("---------------");
		}
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL
