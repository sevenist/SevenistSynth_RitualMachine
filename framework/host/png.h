#pragma once
// A PNG writer for the desktop (tests, screenshots): an RGB565 frame (the colour canvas's, ui/canvas.h), each pixel `scale` x `scale`,
// uncompressed (zlib's "stored" blocks): big files, no dependency. Desktop only (stdio, std::vector).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace png_detail {
inline uint32_t crc(const uint8_t *p, size_t n) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t k = 0; k < 256; k++) {
            uint32_t c = k;
            for (int b = 0; b < 8; b++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[k] = c;
        }
        made = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
inline void be32(std::vector<uint8_t> &v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s)); }
inline void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
    be32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc(td.data(), td.size()));
}
}  // namespace png_detail

// `frame`: w x h pixels, row by row. False when the file could not be written.
inline bool png_write_rgb565(const char *path, const uint16_t *frame, int w, int h, int scale = 1) {
    using namespace png_detail;
    if (scale < 1) scale = 1;
    const int pw = w * scale, ph = h * scale;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(ph) * (1 + 3 * static_cast<size_t>(pw)));
    for (int y = 0; y < ph; y++) {
        raw.push_back(0);                                       // the row's filter: none
        for (int x = 0; x < pw; x++) {
            const uint16_t v = frame[(y / scale) * w + x / scale];
            const uint8_t r = static_cast<uint8_t>((v >> 11) << 3), g = static_cast<uint8_t>(((v >> 5) & 0x3F) << 2), b = static_cast<uint8_t>((v & 0x1F) << 3);
            raw.push_back(static_cast<uint8_t>(r | r >> 5)); raw.push_back(static_cast<uint8_t>(g | g >> 6)); raw.push_back(static_cast<uint8_t>(b | b >> 5));
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;                                      // Adler-32
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    for (size_t at = 0; at < raw.size() || at == 0; at += 65535) {
        const size_t n = raw.size() - at < 65535 ? raw.size() - at : 65535;
        z.push_back(at + n >= raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n)); z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n)); z.push_back(static_cast<uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<long>(at), raw.begin() + static_cast<long>(at + n));
        if (n == 0) break;
    }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, ihdr;
    be32(ihdr, static_cast<uint32_t>(pw)); be32(ihdr, static_cast<uint32_t>(ph));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});                  // 8 bits, RGB
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}
