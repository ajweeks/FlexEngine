#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Window/GL/GLWindowWrapper.hpp"

#include <sstream>

#include "Graphics/GL/GLHelpers.hpp"
#include "Helpers.hpp"

namespace flex
{
	namespace gl
	{
		GLWindowWrapper::GLWindowWrapper(std::string title) :
			GLFWWindowWrapper(title)
		{
		}

		GLWindowWrapper::~GLWindowWrapper()
		{
		}

		void GLWindowWrapper::Create(const glm::vec2i& size, const glm::vec2i& pos)
		{
			if (m_bMoveConsoleToOtherMonitor)
			{
				MoveConsole();
			}

			if (!InitFromConfig())
			{
				m_Size = size;
				m_Position = pos;
			}

			m_FrameBufferSize = m_Size;
			m_LastWindowedSize = m_Size;
			m_StartingPosition = m_Position;
			m_LastWindowedPos = m_Position;

#if _DEBUG
			glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif // _DEBUG

			// Don't hide window when losing focus in Windowed Fullscreen
			glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

			GLFWmonitor* monitor = NULL;
			if (m_CurrentWindowMode == WindowMode::FULLSCREEN)
			{
				monitor = glfwGetPrimaryMonitor();
			}

			m_Window = glfwCreateWindow(m_Size.x, m_Size.y, m_TitleString.c_str(), monitor, NULL);
			if (!m_Window)
			{
				PrintError("Failed to create glfw Window! Exiting...\n");
				glfwTerminate();
				// TODO: Try creating a window manually here
				exit(EXIT_FAILURE);
			}

			glfwSetWindowUserPointer(m_Window, this);

			SetUpCallbacks();

			i32 monitorCount;
			GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

			// If previously the window was on an additional monitor that is no longer present,
			// 
			if (monitorCount == 1)
			{
				const GLFWvidmode* vidMode = glfwGetVideoMode(monitors[0]);
				real monitorWidth = vidMode->width;
				real monitorHeight = vidMode->height;

				if (m_StartingPosition.x < 0)
				{
					m_StartingPosition.x = 100;
				}
				else if (m_StartingPosition.x > monitorWidth)
				{
					m_StartingPosition.x = 100;
				}
				if (m_StartingPosition.y < 0)
				{
					m_StartingPosition.y = 100;
				}
				else if (m_StartingPosition.y > monitorHeight)
				{
					m_StartingPosition.y = 100;
				}
			}

			glfwSetWindowPos(m_Window, m_StartingPosition.x, m_StartingPosition.y);

			glfwFocusWindow(m_Window);
			m_bHasFocus = true;

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

			Print("OpenGL loaded\n");
			Print("Vendor:   %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
			Print("Renderer: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
			Print("Version:  %s\n\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
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

			PrintError("---------------\n\t");
			PrintError("GL Debug message (%i): %s\n", id, message);

			switch (source)
			{
			case GL_DEBUG_SOURCE_API:             PrintError("Source: API"); break;
			case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   PrintError("Source: Window System"); break;
			case GL_DEBUG_SOURCE_SHADER_COMPILER: PrintError("Source: Shader Compiler"); break;
			case GL_DEBUG_SOURCE_THIRD_PARTY:     PrintError("Source: Third Party"); break;
			case GL_DEBUG_SOURCE_APPLICATION:     PrintError("Source: Application"); break;
			case GL_DEBUG_SOURCE_OTHER:           PrintError("Source: Other"); break;
			}
			PrintError("\n\t");

			switch (type)
			{
			case GL_DEBUG_TYPE_ERROR:               PrintError("Type: Error"); break;
			case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: PrintError("Type: Deprecated Behaviour"); break;
			case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  PrintError("Type: Undefined Behaviour"); break;
			case GL_DEBUG_TYPE_PORTABILITY:         PrintError("Type: Portability"); break;
			case GL_DEBUG_TYPE_PERFORMANCE:         PrintError("Type: Performance"); break;
			case GL_DEBUG_TYPE_MARKER:              PrintError("Type: Marker"); break;
			case GL_DEBUG_TYPE_PUSH_GROUP:          PrintError("Type: Push Group"); break;
			case GL_DEBUG_TYPE_POP_GROUP:           PrintError("Type: Pop Group"); break;
			case GL_DEBUG_TYPE_OTHER:               PrintError("Type: Other"); break;
			}
			PrintError("\n\t");

			switch (severity)
			{
			case GL_DEBUG_SEVERITY_HIGH:         PrintError("Severity: high"); break;
			case GL_DEBUG_SEVERITY_MEDIUM:       PrintError("Severity: medium"); break;
			case GL_DEBUG_SEVERITY_LOW:          PrintError("Severity: low"); break;
			case GL_DEBUG_SEVERITY_NOTIFICATION: PrintError("Severity: notification"); break;
			}
			PrintError("\n---------------\n");
		}
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL
