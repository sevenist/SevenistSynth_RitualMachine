// A PNG writer for the tests (no compression: zlib's "stored" blocks), to look at the screen they drew. (TechnoGen's, given the frame.)
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
uint32_t crc_table[256];
void crc_init() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
uint32_t crc(const uint8_t *p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}
void be32(std::vector<uint8_t> &v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s)); }
void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
    be32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
}
}  // namespace

bool write_png(const char *path, const uint16_t *frame, int fw, int fh, int scale) {
    crc_init();
    const int w = fw * scale, h = fh * scale;
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        for (int x = 0; x < w; x++) {
            const uint16_t v = frame[(y / scale) * fw + x / scale];
            const uint8_t r = static_cast<uint8_t>((v >> 11) << 3), g = static_cast<uint8_t>(((v >> 5) & 0x3F) << 2), b = static_cast<uint8_t>((v & 0x1F) << 3);
            raw.push_back(static_cast<uint8_t>(r | r >> 5)); raw.push_back(static_cast<uint8_t>(g | g >> 6)); raw.push_back(static_cast<uint8_t>(b | b >> 5));
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
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
    be32(ihdr, static_cast<uint32_t>(w)); be32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}
