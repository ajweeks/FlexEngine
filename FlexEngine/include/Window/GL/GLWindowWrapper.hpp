#pragma once
#if COMPILE_OPEN_GL

#include "Window/GLFWWindowWrapper.hpp"

namespace flex
{
	namespace gl
	{
		class GLWindowWrapper : public GLFWWindowWrapper
		{
		public:
			GLWindowWrapper(std::string title, GameContext& gameContext);
			virtual ~GLWindowWrapper();

			virtual void Create(glm::vec2i size, glm::vec2i pos) override;

		private:

			GLWindowWrapper(const GLWindowWrapper&) = delete;
			GLWindowWrapper& operator=(const GLWindowWrapper&) = delete;
		};

		void WINAPI glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity,
			GLsizei length, const GLchar *message, const void *userParam);
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL
