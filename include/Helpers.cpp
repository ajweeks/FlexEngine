
#include "Helpers.h"

#include <sstream>
#include <iomanip>

std::string FloatToString(float f, int precision)
{
	std::stringstream stream;

	stream << std::fixed << std::setprecision(precision) << f;

	return stream.str();
}
