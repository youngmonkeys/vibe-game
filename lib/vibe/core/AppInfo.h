#pragma once

#if !defined(VIBE_APP_NAME) || !defined(VIBE_APP_ORG) || !defined(VIBE_APP_ID)
#error "VIBE_APP_NAME, VIBE_APP_ORG and VIBE_APP_ID come from vibe_add_game in CMake"
#endif

namespace vibe::app_info {

inline constexpr const char* kName = VIBE_APP_NAME;
inline constexpr const char* kOrg = VIBE_APP_ORG;
inline constexpr const char* kId = VIBE_APP_ID;

} // namespace vibe::app_info
