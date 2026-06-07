#pragma once
#include <cmath>
#include <algorithm>

namespace moses {

inline constexpr float kMinGain = 1e-9f;
inline constexpr float kPi      = 3.14159265358979323846f;

inline float gainToDb(float x) noexcept {
    return 20.0f * std::log10(std::max(x, kMinGain));
}

inline float dbToGain(float db) noexcept {
    return std::pow(10.0f, 0.05f * db);
}

template <typename T>
inline T clamp(T x, T lo, T hi) noexcept {
    return std::max(lo, std::min(hi, x));
}

} // namespace moses
