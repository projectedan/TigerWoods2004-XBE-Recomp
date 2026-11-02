#pragma once
#include <atomic>
#include <cstdint>
#include <functional>

namespace audio {

constexpr uint32_t kRate = 48000;
inline std::atomic<bool> g_sinc{false};  // band-limited resampling of slow voices, else linear (xdk/dsound.cpp)

// the platform output pulls interleaved stereo float frames from render
void StartOutput(std::function<void(float* out, uint32_t frames)> render);

}  // namespace audio
