#if defined(ARDUINO_ARCH_ESP32)
#include "boards/hwv1/storage_sd.h"
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "boards/hwv1/sd_card.h"

sc::FileHandle SdStorage::open(const char *name) {
    char path[sizeof(File::path)];
    if (snprintf(path, sizeof path, "%s/%s", dir_, name) >= static_cast<int>(sizeof path)) return -1;
    for (int i = 0; i < n_files_; i++) if (!strcmp(file_[i].path, path)) { file_[i].refs++; return i; }   // already known (a sample that is loaded again)
    // No card access here: open() and forget() are called by the control (UI) thread (SampleBank::load / unload, under the engine's
    // lock, which the I/O task's pump holds too), and the card belongs to the I/O task, which also polls it outside that lock: a stat()
    // here (up to 0.4 s in a folder of 1800 files) met the card's alive check and the card was taken for removed (2026-10-11). The file
    // is opened, and its size read, by the I/O task at its first read (fd_of); a file that is not there fails that read.
    int at = -1;
    for (int i = 0; i < n_files_ && at < 0; i++) if (!file_[i].path[0] && file_[i].fd < 0) at = i;   // an entry a forgotten file left
    if (at < 0 && n_files_ < kFiles) at = n_files_;
    if (at < 0) return -1;
    File &f = file_[at];
    memcpy(f.path, path, strlen(path) + 1);
    f.size = kUnknownSize;
    f.fd = -1;
    f.used = 0;
    f.pos = kUnknownPos;
    f.refs = 1;
    if (at == n_files_) n_files_++;
    return at;
}

void SdStorage::forget(sc::FileHandle f) {
    if (f < 0 || f >= n_files_ || !file_[f].path[0]) return;
    if (file_[f].refs > 1) { file_[f].refs--; return; }                              // the same file is loaded once more (taken out, then asked for again)
    file_[f].refs = 0;
    file_[f].size = 0;                                                               // (a read still queued for it fails its bounds check)
    file_[f].path[0] = 0;                                                            // its descriptor is closed by the I/O task (poll): then the entry is free
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
    if (spare_fd_ >= 0 && !strcmp(spare_path_, fl.path)) {               // its header was just read: that descriptor, no walk through the folder
        fl.fd = spare_fd_;
        spare_fd_ = -1;
        fl.pos = kUnknownPos;
    } else {
        fl.fd = ::open(fl.path, O_RDONLY);
        fl.pos = fl.fd >= 0 ? 0 : kUnknownPos;
        stats_.opens++;
    }
    if (fl.fd >= 0 && fl.size == kUnknownSize) {                         // its first open: the size (the bounds of every read)
        struct stat st;
        const off_t end = fstat(fl.fd, &st) == 0 ? st.st_size : lseek(fl.fd, 0, SEEK_END);
        fl.size = end > 0 ? static_cast<uint32_t>(end) : 0;
        fl.pos = kUnknownPos;                                            // (the position may have moved: the next read seeks)
    }
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
        const int64_t t0 = esp_timer_get_time();
        if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) { fl.pos = kUnknownPos; return false; }   // file start on a backward one
        stats_.seek_us += static_cast<uint64_t>(esp_timer_get_time() - t0);
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
    for (int i = 0; i < n_files_; i++)                                   // the descriptors of forgotten files (forget() must not touch the card)
        if (!file_[i].path[0] && file_[i].fd >= 0) { ::close(file_[i].fd); file_[i].fd = -1; file_[i].pos = kUnknownPos; }
    if (count_ == 0) return false;
    const sc::IoRead r = q_[head_];
    if (r.pinned && service_on_ && !served_) return false;               // service() reads it, outside the caller's lock
    const bool ok = served_ ? served_ok_ : execute(r);
    served_ = false;
    head_ = (head_ + 1) % kQueue;
    count_--;
    d = {r.tag, ok};
    return true;
}

bool SdStorage::execute(const sc::IoRead &r) {
    bool ok = false;
    if (r.file >= 0 && r.file < n_files_ && file_[r.file].path[0] && fd_of(r.file) >= 0 &&
        static_cast<uint64_t>(r.offset) + r.bytes <= file_[r.file].size) {
        const int64_t t0 = esp_timer_get_time();
        ok = read_into(file_[r.file], r.offset, r.dst, r.bytes);
        const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
        stats_.read_us += us;
        if (us > stats_.max_us) stats_.max_us = us;
        stats_.bytes += r.bytes;
    }
    if (!ok) stats_.errors++;
    stats_.reads++;
    return ok;
}

// What the control thread may do meanwhile (under the engine's lock, which this does not hold): open() fills a free table entry or
// counts one more use of a path, forget() empties the entry of a file that has no pinned request in flight (SampleBank::can_unload).
// Neither touches the entry being read here, nor the queue, which only the I/O task uses.
bool SdStorage::service() {
    service_on_ = true;
    if (count_ == 0 || served_ || !q_[head_].pinned) return false;
    served_ok_ = execute(q_[head_]);
    served_ = true;
    return true;
}

void SdStorage::keep_spare(const char *path, int fd) {
    if (spare_fd_ >= 0 && spare_fd_ != fd) ::close(spare_fd_);
    spare_fd_ = -1;
    if (fd < 0 || strlen(path) >= sizeof spare_path_) { if (fd >= 0) ::close(fd); return; }
    memcpy(spare_path_, path, strlen(path) + 1);
    spare_fd_ = fd;
}

int SdStorage::take_spare(const char *path) {
    const int fd = spare_fd_;
    spare_fd_ = -1;
    if (fd >= 0 && strcmp(spare_path_, path)) { ::close(fd); return -1; }
    return fd;
}

void SdStorage::close_all() {
    for (int i = 0; i < n_files_; i++) if (file_[i].fd >= 0) { ::close(file_[i].fd); file_[i].fd = -1; file_[i].pos = kUnknownPos; }
    if (spare_fd_ >= 0) { ::close(spare_fd_); spare_fd_ = -1; }
}
#endif
