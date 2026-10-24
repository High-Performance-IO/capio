#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "server/include/storage/file_buffer.hpp"

using Clock           = std::chrono::steady_clock;
constexpr uint64_t MB = 1024 * 1024;

/// Average wall time (microseconds) of fn() across `iters`, with a warmup call first.
template <typename Fn> double bench_us(const int iters, Fn &&fn) {
    fn(); // warmup
    const auto t0 = Clock::now();
    for (int i = 0; i < iters; ++i) {
        fn();
    }
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count() / iters;
}

void print_table(const std::vector<struct Row> &rows);

struct Row {
    const char *op;
    uint64_t size; ///< bytes involved
    int iters;
    double us;   ///< avg wall time
    double mb_s; ///< throughput, when meaningful
};

void print_table(const std::vector<Row> &rows) {
    std::puts("| operation | size (MB) | iters | avg time (us) | throughput (MB/s) |");
    std::puts("|---|---:|---:|---:|---:|");
    for (const Row &r : rows) {
        if (r.mb_s > 0.0) {
            std::printf("| %s | %.0f | %d | %.1f | %.1f |\n", r.op,
                        static_cast<double>(r.size) / MB, r.iters, r.us, r.mb_s);
        } else {
            std::printf("| %s | %.0f | %d | %.1f | n/a |\n", r.op, static_cast<double>(r.size) / MB,
                        r.iters, r.us);
        }
    }
}

int main() {
    std::vector<Row> rows;
    const auto tmp = std::filesystem::temp_directory_path() / "capio_alloc_bench.dat";

    // Heap allocation + release (no data movement).
    for (const uint64_t size : {1 * MB, 16 * MB, 64 * MB, 256 * MB}) {
        const int iters = size < 32 * MB ? 300 : 50; // cheap allocs: average out timer noise
        const double us = bench_us(iters, [size] {
            FileBuffer<HeapBacking> a;
            a.allocate(static_cast<off64_t>(size));
            a.release();
        });
        rows.push_back({"heap alloc+release", size, iters, us, 0.0});
    }

    // Heap allocation then grow (copies `target` bytes through the sector copy),
    // then release: measures reallocation + memcpy throughput.
    for (const uint64_t target : {2 * MB, 32 * MB, 128 * MB}) {
        constexpr int iters = 20;
        const double us     = bench_us(iters, [target] {
            FileBuffer<HeapBacking> a;
            a.allocate(static_cast<off64_t>(target));
            // one sector over the whole buffer; grow doubles it and re-copies the data
            a.grow(static_cast<off64_t>(target * 2), {{0, static_cast<off64_t>(target)}});
            a.release();
        });
        rows.push_back({"heap alloc+grow+release", target, iters, us,
                        static_cast<double>(target) / MB / (us / 1e6)});
    }

    // Persistent-file path: mmap into a temp file, commit (truncate+close), release (unmap).
    for (const uint64_t size : {1 * MB, 16 * MB}) {
        constexpr int iters = 50;
        const double us     = bench_us(iters, [&, size] {
            FileBuffer<MappedBacking> a;
            a.allocate(tmp, static_cast<off64_t>(size));
            a.commit(static_cast<off64_t>(size));
            a.release();
        });
        rows.push_back({"mmap+commit+release", size, iters, us, 0.0});
    }

    std::filesystem::remove(tmp);

    print_table(rows);
    return 0;
}
