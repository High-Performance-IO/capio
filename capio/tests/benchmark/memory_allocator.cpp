// Benchmark for CapioFileMemoryAllocator. Prints a single Markdown table to stdout so CI can
// append it to the run summary (GITHUB_STEP_SUMMARY) to eyeball regressions run over run.
//
// Built with CAPIO_MEMORY_CHECKS_NONE so sizes are not capped by host/cgroup memory: peak
// pressure stays bounded and results are deterministic across CI runners. The cgroup/meminfo
// quota-check path is deliberately not benchmarked (it is dominated by /proc I/O, not by our code).

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "server/include/storage/memory_allocator.hpp"

namespace {

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

} // namespace

int main() {
    std::vector<Row> rows;
    const auto tmp = std::filesystem::temp_directory_path() / "capio_alloc_bench.dat";

    // Heap allocation + release (no data movement).
    for (const uint64_t size : {1 * MB, 16 * MB, 64 * MB, 256 * MB}) {
        const int iters = size < 32 * MB ? 300 : 50; // cheap allocs: average out timer noise
        const double us = bench_us(iters, [size] {
            CapioFileMemoryAllocator a;
            a.allocateHeap(static_cast<off64_t>(size));
            a.release();
        });
        rows.push_back({"heap alloc+release", size, iters, us, 0.0});
    }

    // Heap allocation then resize (copies `target` bytes through `copy`), then release:
    // measures reallocation + memcpy throughput.
    for (const uint64_t target : {2 * MB, 32 * MB, 128 * MB}) {
        constexpr int iters = 20;
        const double us     = bench_us(iters, [target] {
            CapioFileMemoryAllocator a;
            a.allocateHeap(static_cast<off64_t>(MB));
            a.resize(static_cast<off64_t>(target), [target](char *d, const char *) {
                std::memset(d, 1, static_cast<size_t>(target));
            });
            a.release();
        });
        rows.push_back({"heap alloc+resize+release", target, iters, us,
                        static_cast<double>(target) / MB / (us / 1e6)});
    }

    // Persistent-file path: mmap into a temp file, commit (truncate+close), release (unmap).
    for (const uint64_t size : {1 * MB, 16 * MB}) {
        constexpr int iters = 50;
        const double us     = bench_us(iters, [&, size] {
            CapioFileMemoryAllocator a;
            a.allocateMapped(tmp, static_cast<off64_t>(size));
            a.commit(static_cast<off64_t>(size));
            a.release();
        });
        rows.push_back({"mmap+commit+release", size, iters, us, 0.0});
    }

    std::filesystem::remove(tmp);

    print_table(rows);
    return 0;
}

namespace {

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

}; // namespace