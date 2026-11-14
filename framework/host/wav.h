#pragma once
// WAV writers for the desktop (tests, renders to listen to): 16-bit PCM or 32-bit float, interleaved frames. Desktop only (stdio).
// (Reading WAVs: host/sample_convert.h.)
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace wav_detail {
inline void u32(std::FILE *f, uint32_t v) { const uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; std::fwrite(b, 1, 4, f); }
inline void u16(std::FILE *f, uint16_t v) { const uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)}; std::fwrite(b, 1, 2, f); }
// format 1: PCM, 3: IEEE float
inline bool write(const char *path, const void *data, size_t frames, int channels, int rate, int format, int bytes_per_sample) {
    std::FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    const uint32_t block = static_cast<uint32_t>(channels * bytes_per_sample);
    const uint32_t bytes = static_cast<uint32_t>(frames) * block;
    std::fwrite("RIFF", 1, 4, f); u32(f, 36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(f, 16); u16(f, static_cast<uint16_t>(format)); u16(f, static_cast<uint16_t>(channels));
    u32(f, static_cast<uint32_t>(rate)); u32(f, static_cast<uint32_t>(rate) * block); u16(f, static_cast<uint16_t>(block));
    u16(f, static_cast<uint16_t>(8 * bytes_per_sample));
    std::fwrite("data", 1, 4, f); u32(f, bytes);
    const bool ok = std::fwrite(data, 1, bytes, f) == bytes;     // (the host is little-endian, as the file)
    return std::fclose(f) == 0 && ok;
}
}  // namespace wav_detail

// `frames` frames of `channels` samples each. False when the file could not be written.
inline bool wav_write_pcm16(const char *path, const int16_t *x, size_t frames, int channels, int rate) {
    return wav_detail::write(path, x, frames, channels, rate, 1, 2);
}
inline bool wav_write_float(const char *path, const float *x, size_t frames, int channels, int rate) {
    return wav_detail::write(path, x, frames, channels, rate, 3, 4);
}
