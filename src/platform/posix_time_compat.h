#pragma once

// POSIX time-function shims for MSVC/Windows builds.
// timegm / gmtime_r / localtime_r are POSIX; MSVC provides _mkgmtime /
// gmtime_s / localtime_s with different (reversed) argument order.

#include <ctime>

#ifdef _WIN32
inline std::time_t timegm(std::tm* tm) { return _mkgmtime(tm); }
inline std::tm* gmtime_r(const std::time_t* t, std::tm* result)
{
    return ::gmtime_s(result, t) == 0 ? result : nullptr;
}
inline std::tm* localtime_r(const std::time_t* t, std::tm* result)
{
    return ::localtime_s(result, t) == 0 ? result : nullptr;
}
#endif
