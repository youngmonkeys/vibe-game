#pragma once

#if !defined(VIBE_APP_NAME) || !defined(VIBE_APP_ORG) || !defined(VIBE_APP_ID) || !defined(VIBE_ZONE_NAME) || \
    !defined(VIBE_SOCKET_APP_NAME)
#error "VIBE_APP_NAME, VIBE_APP_ORG, VIBE_APP_ID, VIBE_ZONE_NAME and VIBE_SOCKET_APP_NAME come from vibe_add_game in CMake"
#endif

namespace vibe::app_info {

inline constexpr const char* kName = VIBE_APP_NAME;
inline constexpr const char* kOrg = VIBE_APP_ORG;
inline constexpr const char* kId = VIBE_APP_ID;
inline constexpr const char* kZoneName = VIBE_ZONE_NAME;
inline constexpr const char* kSocketAppName = VIBE_SOCKET_APP_NAME;

} // namespace vibe::app_info
