#pragma once

#include <filesystem>
#include <chrono>
#include <ctime>
#include <string>
#include <cstdint>
#include <cstdio>

// -----------------------------------------------------------------------
// Formatting helpers used across multiple translation units.
// Kept in a header because they're short and template-unfriendly if split.
// -----------------------------------------------------------------------

// Converts a filesystem timestamp to a human-readable "YYYY-MM-DD HH:MM" string.
// The file_time_type -> system_clock conversion is done via the offset trick;
// it's not perfect across all filesystems but is the most portable option
// without C++20's clock_cast.
static inline std::string fmtFileTime(const std::filesystem::file_time_type& ft) {
    std::time_t t = 0;
    try {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now()
              + std::chrono::system_clock::now()
        );
        t = std::chrono::system_clock::to_time_t(sctp);
    } catch (...) {
        return "Unknown";
    }

    if (t <= 0) return "Unknown";

    std::tm tm{};
#ifdef _WIN32
    if (localtime_s(&tm, &t) != 0) return "Unknown";
#else
    if (!localtime_r(&t, &tm)) return "Unknown";
#endif

    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return std::string(buf);
}

// Formats a Unix timestamp as "HH:MM" for the settings panel display.
static inline std::string fmtLastSaveTime(int64_t timestamp) {
    if (timestamp <= 0) return "Never";

    std::time_t t = static_cast<std::time_t>(timestamp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M", &tm);
    return std::string(buf);
}

// Formats a byte count into a readable string (B / KB / MB).
static inline std::string humanSize(uint64_t bytes) {
    if (bytes < 1024)
        return std::to_string(bytes) + " B";

    double kb = bytes / 1024.0;
    if (kb < 1024.0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f KB", kb);
        return buf;
    }

    double mb = kb / 1024.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
    return buf;
}
