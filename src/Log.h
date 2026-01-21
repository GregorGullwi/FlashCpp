// ===== src/Log.h (header-only) =====

#pragma once
#include <iostream>
#include <string_view>
#include <cstdint>
#include <array>
#include <format>

namespace FlashCpp {

// Log categories - each can be enabled/disabled independently
enum class LogCategory : uint32_t {
    None        = 0,
    General     = 1 << 0,   // User-facing messages (no prefix, always enabled in release)
    Parser      = 1 << 1,   // General parser operations
    Lexer       = 1 << 2,   // Lexer/tokenizer
    Templates   = 1 << 3,   // Template instantiation
    Symbols     = 1 << 4,   // Symbol table operations
    Types       = 1 << 5,   // Type resolution
    Codegen     = 1 << 6,   // Code generation / IR
    Scope       = 1 << 7,   // Scope enter/exit
    Mangling    = 1 << 8,   // Name mangling
    All         = 0xFFFFFFFF
};

inline LogCategory operator|(LogCategory a, LogCategory b) {
    return static_cast<LogCategory>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline LogCategory operator&(LogCategory a, LogCategory b) {
    return static_cast<LogCategory>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

// Verbosity levels
enum class LogLevel : uint8_t {
    Error   = 0,  // Always shown (unless logging completely disabled)
    Warning = 1,  // Important warnings
    Info    = 2,  // High-level flow
    Debug   = 3,  // Detailed debugging
    Trace   = 4   // Very verbose tracing
};

// Compile-time configuration
// Set these via compiler flags: -DFLASHCPP_LOG_LEVEL=3 -DFLASHCPP_LOG_CATEGORIES=0xFF
#ifndef FLASHCPP_LOG_LEVEL
    #ifdef NDEBUG
        #define FLASHCPP_LOG_LEVEL 2   // Release: up to Info level (General category always enabled regardless of level)
    #else
        #define FLASHCPP_LOG_LEVEL 4   // Debug: up to Debug level
    #endif
#endif

#ifndef FLASHCPP_LOG_CATEGORIES
    #define FLASHCPP_LOG_CATEGORIES 0xFFFFFFFF  // All categories by default
#endif

// Default runtime log level (can be different from compile-time level)
// Set this via compiler flag: -DFLASHCPP_DEFAULT_RUNTIME_LEVEL=2
#ifndef FLASHCPP_DEFAULT_RUNTIME_LEVEL
    #define FLASHCPP_DEFAULT_RUNTIME_LEVEL FLASHCPP_LOG_LEVEL  // Same as compile-time by default
#endif

// ANSI color codes for terminal output
namespace detail {
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* RED     = "\033[31m";
    constexpr const char* YELLOW  = "\033[33m";
    constexpr const char* BLUE    = "\033[34m";
}

// Helper to convert single-bit category flag to array index at compile time
// NOTE: Only works with single-bit categories (General, Parser, etc.), not multi-bit (All)
constexpr size_t categoryToIndex(LogCategory cat) {
    uint32_t val = static_cast<uint32_t>(cat);
    if (val == 0) return 0;
    size_t idx = 0;
    while ((val & 1) == 0) { val >>= 1; idx++; }
    return idx;
}

// Check if a category is a single-bit flag (valid for array indexing)
constexpr bool isSingleBitCategory(LogCategory cat) {
    uint32_t val = static_cast<uint32_t>(cat);
    return val != 0 && (val & (val - 1)) == 0;  // Power of 2 check
}

// Number of log categories (General through Mangling = 9)
constexpr size_t NUM_LOG_CATEGORIES = 9;

// Runtime filter (can be changed at runtime for enabled levels)
struct LogConfig {
    static inline LogLevel runtimeLevel = static_cast<LogLevel>(FLASHCPP_DEFAULT_RUNTIME_LEVEL);
    static inline LogCategory runtimeCategories = static_cast<LogCategory>(FLASHCPP_LOG_CATEGORIES);
    // Fixed-size array for per-category log levels (much faster than unordered_map)
    // Index 0 = General, 1 = Parser, 2 = Lexer, etc.
    // Value of 255 means "use runtimeLevel" (unset)
    // Default-initialized to 255 via aggregate initialization
    static inline std::array<uint8_t, NUM_LOG_CATEGORIES> categoryLevels = []() {
        std::array<uint8_t, NUM_LOG_CATEGORIES> arr;
        arr.fill(255);
        return arr;
    }();
    static inline std::ostream* output_stream = &std::cout;  // Default output stream (errors always go to std::cerr)
    static inline bool use_colors = true;  // Enable/disable ANSI colors

    static void setLevel(LogLevel level) { runtimeLevel = level; }
    static void setLevel(LogCategory cat, LogLevel level) { 
        if (!isSingleBitCategory(cat)) return;  // Ignore multi-bit categories like All
        size_t idx = categoryToIndex(cat);
        if (idx < NUM_LOG_CATEGORIES) {
            categoryLevels[idx] = static_cast<uint8_t>(level);
        }
    }
    static LogLevel getLevelForCategory(LogCategory cat) {
        if (!isSingleBitCategory(cat)) return runtimeLevel;  // Multi-bit categories use runtime level
        size_t idx = categoryToIndex(cat);
        if (idx < NUM_LOG_CATEGORIES && categoryLevels[idx] != 255) {
            return static_cast<LogLevel>(categoryLevels[idx]);
        }
        return runtimeLevel;
    }
    static void setCategories(LogCategory cats) { runtimeCategories = cats; }
    static void enableCategory(LogCategory cat) {
        runtimeCategories = runtimeCategories | cat;
    }
    static void disableCategory(LogCategory cat) {
        runtimeCategories = static_cast<LogCategory>(
            static_cast<uint32_t>(runtimeCategories) & ~static_cast<uint32_t>(cat)
        );
    }
    static void setOutputStream(std::ostream* stream) { output_stream = stream; }
    static void setOutputToStdout() { output_stream = &std::cout; }
    static void setOutputToStderr() { output_stream = &std::cerr; }
    static void setUseColors(bool enable) { use_colors = enable; }
};

// Core logging function
template<LogLevel Level, LogCategory Category>
struct Logger {
    // General category is always enabled at compile time (user-facing messages)
    // For other categories, enable if category is allowed at compile time (runtime level controls execution)
    static constexpr bool enabled =
        (Category == LogCategory::General) ||
        ((static_cast<uint32_t>(Category) & FLASHCPP_LOG_CATEGORIES) != 0);

    template<typename First>
    static void log(First&& first) {
        return log(std::forward<First>(first), "");
    }

    template<typename First, typename... Rest>
    static void log(First&& first, Rest&&... rest) {
        if constexpr (enabled) {
            // General category is always enabled at runtime too
            bool runtime_enabled = (Category == LogCategory::General) ||
                (static_cast<uint8_t>(Level) <= static_cast<uint8_t>(LogConfig::getLevelForCategory(Category)) &&
                 (static_cast<uint32_t>(Category) & static_cast<uint32_t>(LogConfig::runtimeCategories)) != 0);

            if (runtime_enabled) {
                // Errors always go to stderr
                std::ostream& out = (Level == LogLevel::Error) ? std::cerr : *LogConfig::output_stream;

                // General category: no prefix (user-facing messages)
                if constexpr (Category == LogCategory::General) {
                    out << std::forward<First>(first);
                    (out << ... << std::forward<decltype(rest)>(rest));
                    out << "\n";
                } else {
                    // Apply color based on log level
                    if (LogConfig::use_colors) {
                        out << colorCode();
                    }

                    // Print prefix
                    out << "[" << levelName() << "][" << categoryName() << "] ";
                    out << std::forward<First>(first);
                    (out << ... << std::forward<Rest>(rest));

                    // Reset color
                    if (LogConfig::use_colors) {
                        out << detail::RESET;
                    }
                    out << "\n";
                }
            }
        }
    }

    static constexpr const char* colorCode() {
        switch (Level) {
            case LogLevel::Error:   return detail::RED;
            case LogLevel::Warning: return detail::YELLOW;
            case LogLevel::Trace:   return detail::BLUE;
            default:                return "";  // No color for Info, Debug
        }
    }

    static constexpr std::string_view levelName() {
        switch (Level) {
            case LogLevel::Error:   return "ERROR";
            case LogLevel::Warning: return "WARN ";
            case LogLevel::Info:    return "INFO ";
            case LogLevel::Debug:   return "DEBUG";
            case LogLevel::Trace:   return "TRACE";
            default:                return "?????";
        }
    }

    static constexpr std::string_view categoryName() {
        switch (Category) {
            case LogCategory::None:      return "None";
            case LogCategory::General:   return "General";
            case LogCategory::Parser:    return "Parser";
            case LogCategory::Lexer:     return "Lexer";
            case LogCategory::Templates: return "Templates";
            case LogCategory::Symbols:   return "Symbols";
            case LogCategory::Types:     return "Types";
            case LogCategory::Codegen:   return "Codegen";
            case LogCategory::Scope:     return "Scope";
            case LogCategory::Mangling:  return "Mangling";
            case LogCategory::All:       return "All";
            default:                     return "Unknown";  // Multi-category bitmask
        }
    }
};

// Check if logging is enabled for a specific category/level combination
// This allows avoiding construction of expensive debug strings when logging is disabled
template<LogLevel Level, LogCategory Category>
constexpr bool isLogEnabled() {
    // Compile-time check: General category is always enabled
    // For other categories, enable if category is allowed at compile time
    constexpr bool compile_time_enabled =
        (Category == LogCategory::General) ||
        ((static_cast<uint32_t>(Category) & FLASHCPP_LOG_CATEGORIES) != 0);

    if constexpr (!compile_time_enabled) {
        return false;
    }

    // Runtime check
    return (Category == LogCategory::General) ||
           (static_cast<uint8_t>(Level) <= static_cast<uint8_t>(LogConfig::getLevelForCategory(Category)) &&
            (static_cast<uint32_t>(Category) & static_cast<uint32_t>(LogConfig::runtimeCategories)) != 0);
}

// Convenience macros - zero overhead when disabled at compile time
// Use preprocessor #if to completely eliminate code

// Helper macros for level values (must match LogLevel enum)
#define FLASH_LEVEL_Error 0
#define FLASH_LEVEL_Warning 1
#define FLASH_LEVEL_Info 2
#define FLASH_LEVEL_Debug 3
#define FLASH_LEVEL_Trace 4

// Helper function to suppress unused variable warnings when logging is disabled
// Takes any arguments and does nothing - compiler will optimize this away
template<typename... Args>
inline void flash_log_unused(Args&&...) {}

// FLASH_LOG macro - completely eliminated at preprocessing when level is disabled
// Now with runtime check BEFORE evaluating arguments to avoid expensive string construction
#if FLASHCPP_LOG_LEVEL >= 4  // Trace
#define FLASH_LOG_Trace_IMPL(cat, ...) \
    do { \
        if (::FlashCpp::isLogEnabled<::FlashCpp::LogLevel::Trace, ::FlashCpp::LogCategory::cat>()) { \
            ::FlashCpp::Logger<::FlashCpp::LogLevel::Trace, ::FlashCpp::LogCategory::cat>::log(__VA_ARGS__); \
        } \
    } while(0)
#else
#define FLASH_LOG_Trace_IMPL(cat, ...) ::FlashCpp::flash_log_unused(__VA_ARGS__)
#endif

#if FLASHCPP_LOG_LEVEL >= 3  // Debug
#define FLASH_LOG_Debug_IMPL(cat, ...) \
    do { \
        if (::FlashCpp::isLogEnabled<::FlashCpp::LogLevel::Debug, ::FlashCpp::LogCategory::cat>()) { \
            ::FlashCpp::Logger<::FlashCpp::LogLevel::Debug, ::FlashCpp::LogCategory::cat>::log(__VA_ARGS__); \
        } \
    } while(0)
#else
#define FLASH_LOG_Debug_IMPL(cat, ...) ::FlashCpp::flash_log_unused(__VA_ARGS__)
#endif

#if FLASHCPP_LOG_LEVEL >= 2  // Info
#define FLASH_LOG_Info_IMPL(cat, ...) \
    do { \
        if (::FlashCpp::isLogEnabled<::FlashCpp::LogLevel::Info, ::FlashCpp::LogCategory::cat>()) { \
            ::FlashCpp::Logger<::FlashCpp::LogLevel::Info, ::FlashCpp::LogCategory::cat>::log(__VA_ARGS__); \
        } \
    } while(0)
#else
#define FLASH_LOG_Info_IMPL(cat, ...) ::FlashCpp::flash_log_unused(__VA_ARGS__)
#endif

#if FLASHCPP_LOG_LEVEL >= 1  // Warning
#define FLASH_LOG_Warning_IMPL(cat, ...) \
    do { \
        if (::FlashCpp::isLogEnabled<::FlashCpp::LogLevel::Warning, ::FlashCpp::LogCategory::cat>()) { \
            ::FlashCpp::Logger<::FlashCpp::LogLevel::Warning, ::FlashCpp::LogCategory::cat>::log(__VA_ARGS__); \
        } \
    } while(0)
#else
#define FLASH_LOG_Warning_IMPL(cat, ...) ::FlashCpp::flash_log_unused(__VA_ARGS__)
#endif

// Error is always enabled - also add runtime check for consistency
#define FLASH_LOG_Error_IMPL(cat, ...) \
    do { \
        if (::FlashCpp::isLogEnabled<::FlashCpp::LogLevel::Error, ::FlashCpp::LogCategory::cat>()) { \
            ::FlashCpp::Logger<::FlashCpp::LogLevel::Error, ::FlashCpp::LogCategory::cat>::log(__VA_ARGS__); \
        } \
    } while(0)

// Main FLASH_LOG macro that dispatches based on level
#define FLASH_LOG(cat, level, ...) FLASH_LOG_##level##_IMPL(cat, __VA_ARGS__)

#define FLASH_LOG_ENABLED(cat, level) \
    ((FLASH_LEVEL_##level <= FLASHCPP_LOG_LEVEL) && \
     ::FlashCpp::isLogEnabled<::FlashCpp::LogLevel::level, ::FlashCpp::LogCategory::cat>())

// std::format version - use when you know all arguments are formattable
#if FLASHCPP_LOG_LEVEL >= 4  // Trace
#define FLASH_LOG_FORMAT_Trace_IMPL(cat, fmt, ...) \
    do { \
        bool runtime_enabled = (::FlashCpp::LogCategory::cat == ::FlashCpp::LogCategory::General) || \
            (static_cast<uint8_t>(::FlashCpp::LogLevel::Trace) <= static_cast<uint8_t>(::FlashCpp::LogConfig::getLevelForCategory(::FlashCpp::LogCategory::cat)) && \
             (static_cast<uint32_t>(::FlashCpp::LogCategory::cat) & static_cast<uint32_t>(::FlashCpp::LogConfig::runtimeCategories)) != 0); \
        if (runtime_enabled) { \
            std::ostream& flash_log_out = *::FlashCpp::LogConfig::output_stream; \
            if (::FlashCpp::LogConfig::use_colors) { \
                flash_log_out << ::FlashCpp::Logger<::FlashCpp::LogLevel::Trace, ::FlashCpp::LogCategory::cat>::colorCode(); \
            } \
            flash_log_out << "[" << ::FlashCpp::Logger<::FlashCpp::LogLevel::Trace, ::FlashCpp::LogCategory::cat>::levelName() \
                          << "][" << ::FlashCpp::Logger<::FlashCpp::LogLevel::Trace, ::FlashCpp::LogCategory::cat>::categoryName() << "] " \
                          << std::format(fmt, __VA_ARGS__); \
            if (::FlashCpp::LogConfig::use_colors) { \
                flash_log_out << ::FlashCpp::detail::RESET; \
            } \
            flash_log_out << "\n"; \
        } \
    } while(0)
#else
#define FLASH_LOG_FORMAT_Trace_IMPL(cat, fmt, ...) ::FlashCpp::flash_log_unused(fmt, __VA_ARGS__)
#endif

#if FLASHCPP_LOG_LEVEL >= 3  // Debug
#define FLASH_LOG_FORMAT_Debug_IMPL(cat, fmt, ...) \
    do { \
        bool runtime_enabled = (::FlashCpp::LogCategory::cat == ::FlashCpp::LogCategory::General) || \
            (static_cast<uint8_t>(::FlashCpp::LogLevel::Debug) <= static_cast<uint8_t>(::FlashCpp::LogConfig::getLevelForCategory(::FlashCpp::LogCategory::cat)) && \
             (static_cast<uint32_t>(::FlashCpp::LogCategory::cat) & static_cast<uint32_t>(::FlashCpp::LogConfig::runtimeCategories)) != 0); \
        if (runtime_enabled) { \
            std::ostream& flash_log_out = *::FlashCpp::LogConfig::output_stream; \
            if (::FlashCpp::LogConfig::use_colors) { \
                flash_log_out << ::FlashCpp::Logger<::FlashCpp::LogLevel::Debug, ::FlashCpp::LogCategory::cat>::colorCode(); \
            } \
            flash_log_out << "[" << ::FlashCpp::Logger<::FlashCpp::LogLevel::Debug, ::FlashCpp::LogCategory::cat>::levelName() \
                          << "][" << ::FlashCpp::Logger<::FlashCpp::LogLevel::Debug, ::FlashCpp::LogCategory::cat>::categoryName() << "] " \
                          << std::format(fmt, __VA_ARGS__); \
            if (::FlashCpp::LogConfig::use_colors) { \
                flash_log_out << ::FlashCpp::detail::RESET; \
            } \
            flash_log_out << "\n"; \
        } \
    } while(0)
#else
#define FLASH_LOG_FORMAT_Debug_IMPL(cat, fmt, ...) ::FlashCpp::flash_log_unused(fmt, __VA_ARGS__)
#endif

#if FLASHCPP_LOG_LEVEL >= 2  // Info
#define FLASH_LOG_FORMAT_Info_IMPL(cat, fmt, ...) \
    do { \
        bool runtime_enabled = (::FlashCpp::LogCategory::cat == ::FlashCpp::LogCategory::General) || \
            (static_cast<uint8_t>(::FlashCpp::LogLevel::Info) <= static_cast<uint8_t>(::FlashCpp::LogConfig::getLevelForCategory(::FlashCpp::LogCategory::cat)) && \
             (static_cast<uint32_t>(::FlashCpp::LogCategory::cat) & static_cast<uint32_t>(::FlashCpp::LogConfig::runtimeCategories)) != 0); \
        if (runtime_enabled) { \
            std::ostream& flash_log_out = *::FlashCpp::LogConfig::output_stream; \
            if (::FlashCpp::LogConfig::use_colors) { \
                flash_log_out << ::FlashCpp::Logger<::FlashCpp::LogLevel::Info, ::FlashCpp::LogCategory::cat>::colorCode(); \
            } \
            flash_log_out << "[" << ::FlashCpp::Logger<::FlashCpp::LogLevel::Info, ::FlashCpp::LogCategory::cat>::levelName() \
                          << "][" << ::FlashCpp::Logger<::FlashCpp::LogLevel::Info, ::FlashCpp::LogCategory::cat>::categoryName() << "] " \
                          << std::format(fmt, __VA_ARGS__); \
            if (::FlashCpp::LogConfig::use_colors) { \
                flash_log_out << ::FlashCpp::detail::RESET; \
            } \
            flash_log_out << "\n"; \
        } \
    } while(0)
#else
#define FLASH_LOG_FORMAT_Info_IMPL(cat, fmt, ...) ::FlashCpp::flash_log_unused(fmt, __VA_ARGS__)
#endif

#if FLASHCPP_LOG_LEVEL >= 1  // Warning
#define FLASH_LOG_FORMAT_Warning_IMPL(cat, fmt, ...) \
    do { \
        bool runtime_enabled = (::FlashCpp::LogCategory::cat == ::FlashCpp::LogCategory::General) || \
            (static_cast<uint8_t>(::FlashCpp::LogLevel::Warning) <= static_cast<uint8_t>(::FlashCpp::LogConfig::getLevelForCategory(::FlashCpp::LogCategory::cat)) && \
             (static_cast<uint32_t>(::FlashCpp::LogCategory::cat) & static_cast<uint32_t>(::FlashCpp::LogConfig::runtimeCategories)) != 0); \
        if (runtime_enabled) { \
            std::ostream& flash_log_out = *::FlashCpp::LogConfig::output_stream; \
            if (::FlashCpp::LogConfig::use_colors) { \
                flash_log_out << ::FlashCpp::Logger<::FlashCpp::LogLevel::Warning, ::FlashCpp::LogCategory::cat>::colorCode(); \
            } \
            flash_log_out << "[" << ::FlashCpp::Logger<::FlashCpp::LogLevel::Warning, ::FlashCpp::LogCategory::cat>::levelName() \
                          << "][" << ::FlashCpp::Logger<::FlashCpp::LogLevel::Warning, ::FlashCpp::LogCategory::cat>::categoryName() << "] " \
                          << std::format(fmt, __VA_ARGS__); \
            if (::FlashCpp::LogConfig::use_colors) { \
                flash_log_out << ::FlashCpp::detail::RESET; \
            } \
            flash_log_out << "\n"; \
        } \
    } while(0)
#else
#define FLASH_LOG_FORMAT_Warning_IMPL(cat, fmt, ...) ::FlashCpp::flash_log_unused(fmt, __VA_ARGS__)
#endif

// Error is always enabled
#define FLASH_LOG_FORMAT_Error_IMPL(cat, fmt, ...) \
    do { \
        bool runtime_enabled = (::FlashCpp::LogCategory::cat == ::FlashCpp::LogCategory::General) || \
            (static_cast<uint8_t>(::FlashCpp::LogLevel::Error) <= static_cast<uint8_t>(::FlashCpp::LogConfig::getLevelForCategory(::FlashCpp::LogCategory::cat)) && \
             (static_cast<uint32_t>(::FlashCpp::LogCategory::cat) & static_cast<uint32_t>(::FlashCpp::LogConfig::runtimeCategories)) != 0); \
        if (runtime_enabled) { \
            std::ostream& flash_log_out = std::cerr; \
            if (::FlashCpp::LogConfig::use_colors) { \
                flash_log_out << ::FlashCpp::Logger<::FlashCpp::LogLevel::Error, ::FlashCpp::LogCategory::cat>::colorCode(); \
            } \
            flash_log_out << "[" << ::FlashCpp::Logger<::FlashCpp::LogLevel::Error, ::FlashCpp::LogCategory::cat>::levelName() \
                          << "][" << ::FlashCpp::Logger<::FlashCpp::LogLevel::Error, ::FlashCpp::LogCategory::cat>::categoryName() << "] " \
                          << std::format(fmt, __VA_ARGS__); \
            if (::FlashCpp::LogConfig::use_colors) { \
                flash_log_out << ::FlashCpp::detail::RESET; \
            } \
            flash_log_out << "\n"; \
        } \
    } while(0)

// Main FLASH_LOG_FORMAT macro that dispatches based on level
#define FLASH_LOG_FORMAT(cat, level, fmt, ...) FLASH_LOG_FORMAT_##level##_IMPL(cat, fmt, __VA_ARGS__)

} // namespace FlashCpp
