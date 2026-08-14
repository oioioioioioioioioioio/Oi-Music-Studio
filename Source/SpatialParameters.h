#pragma once

#include <cstdint>

namespace oi
{
enum class SpatialAttenuation : std::uint8_t
{
    inverseSquare,
    linear,
    customCurve
};

// Parameters are stored per track. The renderer deliberately starts bypassed so
// imported projects retain conventional stereo mixing until spatial rendering is enabled.
struct SpatialParameters
{
    bool enabled = false;
    float azimuth = 0.0f;
    float elevation = 0.0f;
    float distance = 1.0f;
    float orbitSpeed = 0.0f;
    float spread = 25.0f;
    float directivity = 35.0f;
    SpatialAttenuation attenuation = SpatialAttenuation::inverseSquare;
    bool airAbsorption = false;
};
} // namespace oi
