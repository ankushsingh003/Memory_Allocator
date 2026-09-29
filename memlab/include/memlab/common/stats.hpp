#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace memlab {

// ─── Percentile helpers ───────────────────────────────────────────────────────

/**
 * Compute a percentile from a SORTED range [begin, end).
 * p must be in [0.0, 100.0].
 * Uses the nearest-rank method.
 */
template <typename It>
double percentile_sorted(It begin, It end, double p) {
    assert(begin != end);
    std::size_t n = static_cast<std::size_t>(std::distance(begin, end));
    std::size_t idx = static_cast<std::size_t>(std::ceil(p / 100.0 * n));
    if (idx > 0) --idx;              // convert to 0-indexed
    idx = std::min(idx, n - 1);
    return static_cast<double>(*(begin + static_cast<std::ptrdiff_t>(idx)));
}

/**
 * Convenience: sort a copy and compute multiple percentiles at once.
 * Returns {p50, p99, p999} in nanoseconds.
 */
struct Percentiles {
    double p50{}, p99{}, p999{};
};

inline Percentiles compute_percentiles(std::vector<int64_t> samples) {
    std::sort(samples.begin(), samples.end());
    return {
        percentile_sorted(samples.begin(), samples.end(), 50.0),
        percentile_sorted(samples.begin(), samples.end(), 99.0),
        percentile_sorted(samples.begin(), samples.end(), 99.9)
    };
}

inline double mean(const std::vector<int64_t>& v) {
    if (v.empty()) return 0.0;
    return static_cast<double>(
        std::accumulate(v.begin(), v.end(), int64_t{0})) / static_cast<double>(v.size());
}

inline double stddev(const std::vector<int64_t>& v) {
    if (v.size() < 2) return 0.0;
    double m = mean(v);
    double sq = 0.0;
    for (auto x : v) { double d = x - m; sq += d * d; }
    return std::sqrt(sq / static_cast<double>(v.size() - 1));
}

// ─── CsvWriter ────────────────────────────────────────────────────────────────

/**
 * Simple append-mode CSV writer.
 *
 * Usage:
 *   CsvWriter csv("results/prefault_bench.csv");
 *   csv.write_header({"policy","p50_ns","p99_ns","p999_ns","startup_ms"});
 *   csv.write_row({"Lazy", p.p50, p.p99, p.p999, startup_ms});
 */
class CsvWriter {
public:
    explicit CsvWriter(const std::string& path, bool append = false) {
        auto mode = std::ios::out | (append ? std::ios::app : std::ios::trunc);
        _file.open(path, mode);
        if (!_file.is_open())
            throw std::runtime_error("CsvWriter: cannot open " + path);
    }

    void write_header(const std::vector<std::string>& cols) {
        write_row(cols);
    }

    template <typename... Args>
    void write_row(Args&&... args) {
        std::vector<std::string> cells = {to_str(std::forward<Args>(args))...};
        write_row(cells);
    }

    void write_row(const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (i) _file << ',';
            _file << cells[i];
        }
        _file << '\n';
        _file.flush();
    }

private:
    std::ofstream _file;

    template <typename T>
    static std::string to_str(T&& v) {
        std::ostringstream oss;
        oss << v;
        return oss.str();
    }
};

// ─── MachineInfo ─────────────────────────────────────────────────────────────

/**
 * Returns a single-line string with basic environment metadata.
 * Written as a comment row at the top of every CSV so results are reproducible.
 *
 * Format: # compiler=..., build=..., date=...
 */
inline std::string machine_info_comment() {
    std::ostringstream oss;
    oss << "# ";
#if defined(__clang__)
    oss << "compiler=clang-" << __clang_major__ << "." << __clang_minor__;
#elif defined(__GNUC__)
    oss << "compiler=gcc-" << __GNUC__ << "." << __GNUC_MINOR__;
#elif defined(_MSC_VER)
    oss << "compiler=msvc-" << _MSC_VER;
#else
    oss << "compiler=unknown";
#endif

#if defined(NDEBUG)
    oss << " build=Release";
#else
    oss << " build=Debug";
#endif
    return oss.str();
}

} // namespace memlab
