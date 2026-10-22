#include "TimeUtils.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

long long TimeUtils::Parse(const std::string& strTime)
{
	int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;
	char c1 = 0, c2 = 0, sep = 0, c3 = 0, c4 = 0;

	const int n = std::sscanf(strTime.c_str(), "%d%c%d%c%d%c%d%c%d%c%d", &Y, &c1, &M, &c2, &D, &sep, &h, &c3, &m, &c4, &s);

	if(n >= 5 && c1 == '-' && c2 == '-')
	{
		if(M < 1 || M > 12 || D < 1 || D > 31)
			return -1;

		std::tm tm{};
		tm.tm_year = Y - 1900;
		tm.tm_mon  = M - 1;
		tm.tm_mday = D;
		if(n >= 9 && c3 == ':')
		{
			tm.tm_hour = h;
			tm.tm_min  = m;
			if(n >= 11 && c4 == ':')
				tm.tm_sec = s;
		}
		return static_cast<long long>(timegm(&tm));
	}

	char* end = nullptr;
	const long long v = std::strtoll(strTime.c_str(), &end, 10);
	if(end != strTime.c_str() && *end == '\0' && v > 0)
		return v > 100000000000LL ? v / 1000 : v;

	return -1;
}

std::string TimeUtils::Format(long long nTime)
{
	const std::time_t t = static_cast<std::time_t>(nTime);
	std::tm tm{};
	gmtime_r(&t, &tm);
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
	return buf;
}
