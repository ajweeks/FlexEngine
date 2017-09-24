#pragma once

#include <glm/vec2.hpp>

namespace glm
{
	typedef tvec2<int> vec2i;
}

namespace flex
{
	typedef glm::uint RenderID;
	typedef glm::uint ShaderID;
	typedef glm::uint MaterialID;
	typedef glm::uint PointLightID;
	typedef glm::uint DirectionalLightID;
} // namespace flex
