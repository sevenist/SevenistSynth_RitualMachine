#pragma once
// SimStorage: memory-backed StorageDevice with a timing model of a slow card (desktop and tests only).
//   latency_us   fixed cost per request (command + seek)
//   bytes_per_s  sustained transfer rate (0 = unlimited)
//   stall_every  every N-th request takes stall_us extra (garbage collection of a cheap SD card)
// The device is serial like an SPI card: a request starts when the previous one is done.
#include <cstring>
#include <string>
#include <vector>
#include "engine/sampler/storage.h"

namespace sc {

struct StorageModel {
    uint32_t latency_us = 0;
    uint32_t bytes_per_s = 0;
    uint32_t stall_every = 0;
    uint32_t stall_us = 0;
};

class SimStorage : public StorageDevice {
public:
    explicit SimStorage(const StorageModel &m = StorageModel{}) : model_(m) {}

    FileHandle add_file(const char *name, const std::vector<uint8_t> &data) {
        files_.push_back({name, data});
        return static_cast<FileHandle>(files_.size() - 1);
    }
    void set_model(const StorageModel &m) { model_ = m; }
    uint64_t total_reads() const { return reads_; }
    uint64_t total_bytes() const { return bytes_; }

    FileHandle open(const char *name) override {
        for (size_t i = 0; i < files_.size(); i++) if (files_[i].name == name) return static_cast<FileHandle>(i);
        return -1;
    }
    uint32_t size(FileHandle f) const override { return f >= 0 && f < static_cast<int>(files_.size()) ? static_cast<uint32_t>(files_[static_cast<size_t>(f)].data.size()) : 0; }

    bool submit(const IoRead &r, uint64_t now_us) override {
        if (count_ >= kQueue) return false;
        uint64_t start = now_us > busy_until_ ? now_us : busy_until_;
        uint64_t dur = model_.latency_us;
        if (model_.bytes_per_s) dur += static_cast<uint64_t>(r.bytes) * 1000000ull / model_.bytes_per_s;
        if (model_.stall_every && ++issued_ % model_.stall_every == 0) dur += model_.stall_us;
        busy_until_ = start + dur;
        q_[(head_ + count_) % kQueue] = {r, busy_until_};
        count_++;
        return true;
    }

    bool poll(IoDone &d, uint64_t now_us) override {
        if (count_ == 0 || q_[head_].done_at > now_us) return false;
        const Item &it = q_[head_];
        bool ok = it.r.file >= 0 && it.r.file < static_cast<int>(files_.size());
        if (ok) {
            const auto &data = files_[static_cast<size_t>(it.r.file)].data;
            ok = static_cast<uint64_t>(it.r.offset) + it.r.bytes <= data.size();
            if (ok) std::memcpy(it.r.dst, data.data() + it.r.offset, it.r.bytes);
        }
        d = {it.r.tag, ok};
        reads_++;
        bytes_ += it.r.bytes;
        head_ = (head_ + 1) % kQueue;
        count_--;
        return true;
    }
    int pending() const override { return count_; }

private:
    struct File { std::string name; std::vector<uint8_t> data; };
    struct Item { IoRead r; uint64_t done_at; };
    static constexpr int kQueue = 32;
    StorageModel model_;
    std::vector<File> files_;
    Item q_[kQueue] = {};
    int head_ = 0, count_ = 0;
    uint64_t busy_until_ = 0, reads_ = 0, bytes_ = 0;
    uint32_t issued_ = 0;
};

}  // namespace sc
