#pragma once
#include <string>

namespace TimeUtils
{
	long long Parse(const std::string& strTime);

	std::string Format(long long nTime);
}
