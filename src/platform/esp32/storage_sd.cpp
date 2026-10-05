#if defined(ARDUINO_ARCH_ESP32)
#include "platform/esp32/storage_sd.h"
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "platform/esp32/sd_card.h"

sc::FileHandle SdStorage::open(const char *name) {
    char path[sizeof(File::path)];
    if (snprintf(path, sizeof path, SD_SAMPLE_DIR "/%s", name) >= static_cast<int>(sizeof path)) return -1;
    for (int i = 0; i < n_files_; i++) if (!strcmp(file_[i].path, path)) return i;       // already known (a sample that is loaded again)
    struct stat st;
    if (n_files_ >= kFiles || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    File &f = file_[n_files_];
    memcpy(f.path, path, strlen(path) + 1);
    f.size = static_cast<uint32_t>(st.st_size);
    f.fd = -1;
    f.used = 0;
    f.pos = kUnknownPos;
    return n_files_++;
}

int SdStorage::fd_of(int f) {
    File &fl = file_[f];
    fl.used = ++tick_;
    if (fl.fd >= 0) return fl.fd;
    int open_n = 0, lru = -1;
    for (int i = 0; i < n_files_; i++) {
        if (file_[i].fd < 0) continue;
        open_n++;
        if (lru < 0 || file_[i].used < file_[lru].used) lru = i;
    }
    if (open_n >= kOpenFds && lru >= 0) { ::close(file_[lru].fd); file_[lru].fd = -1; file_[lru].pos = kUnknownPos; }
    fl.fd = ::open(fl.path, O_RDONLY);
    fl.pos = fl.fd >= 0 ? 0 : kUnknownPos;
    stats_.opens++;
    return fl.fd;
}

bool SdStorage::submit(const sc::IoRead &r, uint64_t) {
    if (count_ >= kQueue) return false;
    q_[(head_ + count_) % kQueue] = r;
    count_++;
    return true;
}

// `bytes` at `offset` of an open file into `dst`. A destination the SD host can fill by DMA (internal RAM, 4-byte aligned) is read directly;
// anything else (PSRAM) goes through the internal bounce buffer in chunks.
bool SdStorage::read_into(File &fl, uint32_t offset, void *dst, uint32_t bytes) {
    const int fd = fl.fd;
    if (fl.pos != offset) {                                              // a sequential stream needs no seek: the FAT cluster chain is walked from the
        if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) { fl.pos = kUnknownPos; return false; }   // file start on a backward one
        stats_.seeks++;
    }
    const bool direct = esp_ptr_dma_capable(dst) && (reinterpret_cast<uintptr_t>(dst) & 3u) == 0;
    if (!direct && !bounce_) bounce_ = static_cast<uint8_t *>(heap_caps_malloc(kBounceBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    bool ok = true;
    if (direct || !bounce_) {                                            // (no bounce buffer: let the driver do its slow single-sector path)
        ok = read(fd, dst, bytes) == static_cast<ssize_t>(bytes);
    } else {
        for (uint32_t done = 0; done < bytes && ok;) {
            const uint32_t n = bytes - done < kBounceBytes ? bytes - done : kBounceBytes;
            ok = read(fd, bounce_, n) == static_cast<ssize_t>(n);
            if (ok) { memcpy(static_cast<uint8_t *>(dst) + done, bounce_, n); done += n; }
        }
    }
    fl.pos = ok ? offset + bytes : kUnknownPos;
    return ok;
}

bool SdStorage::poll(sc::IoDone &d, uint64_t) {
    if (count_ == 0) return false;
    const sc::IoRead r = q_[head_];
    head_ = (head_ + 1) % kQueue;
    count_--;
    bool ok = false;
    if (r.file >= 0 && r.file < n_files_ && static_cast<uint64_t>(r.offset) + r.bytes <= file_[r.file].size && fd_of(r.file) >= 0) {
        const int64_t t0 = esp_timer_get_time();
        ok = read_into(file_[r.file], r.offset, r.dst, r.bytes);
        const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
        stats_.read_us += us;
        if (us > stats_.max_us) stats_.max_us = us;
        stats_.bytes += r.bytes;
    }
    if (!ok) stats_.errors++;
    stats_.reads++;
    d = {r.tag, ok};
    return true;
}

void SdStorage::close_all() {
    for (int i = 0; i < n_files_; i++) if (file_[i].fd >= 0) { ::close(file_[i].fd); file_[i].fd = -1; file_[i].pos = kUnknownPos; }
}
#endif
