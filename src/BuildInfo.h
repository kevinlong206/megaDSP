#pragma once

#include <string_view>

#ifndef MEGADSP_VERSION
#define MEGADSP_VERSION "unknown"
#endif

#ifndef MEGADSP_COMMIT_HASH
#define MEGADSP_COMMIT_HASH "unknown"
#endif

namespace megadsp
{
inline constexpr std::string_view buildVersion { MEGADSP_VERSION };
inline constexpr std::string_view buildCommitHash { MEGADSP_COMMIT_HASH };
} // namespace megadsp
