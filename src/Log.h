// ===== src/Log.h (header-only) =====

#pragma once
#include <iostream>
#include <string_view>
#include <cstdint>
#include <unordered_map>
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

// Runtime filter (can be changed at runtime for enabled levels)
struct LogConfig {
    static inline LogLevel runtimeLevel = static_cast<LogLevel>(FLASHCPP_DEFAULT_RUNTIME_LEVEL);
    static inline LogCategory runtimeCategories = static_cast<LogCategory>(FLASHCPP_LOG_CATEGORIES);
    static inline std::unordered_map<uint32_t, LogLevel> categoryLevels;  // Per-category log levels
    static inline std::ostream* output_stream = &std::cout;  // Default output stream (errors always go to std::cerr)
    static inline bool use_colors = true;  // Enable/disable ANSI colors

    static void setLevel(LogLevel level) { runtimeLevel = level; }
    static void setLevel(LogCategory cat, LogLevel level) { categoryLevels[static_cast<uint32_t>(cat)] = level; }
    static LogLevel getLevelForCategory(LogCategory cat) {
        auto it = categoryLevels.find(static_cast<uint32_t>(cat));
        return it != categoryLevels.end() ? it->second : runtimeLevel;
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
#define FLASH_LOG(cat, level, ...) \
    do { \
        if constexpr (::FlashCpp::Logger<::FlashCpp::LogLevel::level, ::FlashCpp::LogCategory::cat>::enabled) { \
            if (::FlashCpp::isLogEnabled<::FlashCpp::LogLevel::level, ::FlashCpp::LogCategory::cat>()) { \
                ::FlashCpp::Logger<::FlashCpp::LogLevel::level, ::FlashCpp::LogCategory::cat>::log(__VA_ARGS__); \
            } \
        } \
    } while(0)
#define FLASH_LOG_ENABLED(cat, level) ::FlashCpp::isLogEnabled<::FlashCpp::LogLevel::level, ::FlashCpp::LogCategory::cat>()

// std::format version - use when you know all arguments are formattable
#define FLASH_LOG_FORMAT(cat, level, fmt, ...) \
    do { \
        if constexpr (::FlashCpp::Logger<::FlashCpp::LogLevel::level, ::FlashCpp::LogCategory::cat>::enabled) { \
            bool runtime_enabled = (::FlashCpp::LogCategory::cat == ::FlashCpp::LogCategory::General) || \
                (static_cast<uint8_t>(::FlashCpp::LogLevel::level) <= static_cast<uint8_t>(::FlashCpp::LogConfig::getLevelForCategory(::FlashCpp::LogCategory::cat)) && \
                 (static_cast<uint32_t>(::FlashCpp::LogCategory::cat) & static_cast<uint32_t>(::FlashCpp::LogConfig::runtimeCategories)) != 0); \
            if (runtime_enabled) { \
                std::ostream& flash_log_out = (::FlashCpp::LogLevel::level == ::FlashCpp::LogLevel::Error) ? std::cerr : *::FlashCpp::LogConfig::output_stream; \
                if constexpr (::FlashCpp::LogCategory::cat == ::FlashCpp::LogCategory::General) { \
                    flash_log_out << std::format(fmt, __VA_ARGS__) << "\n"; \
                } else { \
                    if (::FlashCpp::LogConfig::use_colors) { \
                        flash_log_out << ::FlashCpp::Logger<::FlashCpp::LogLevel::level, ::FlashCpp::LogCategory::cat>::colorCode(); \
                    } \
                    flash_log_out << "[" << ::FlashCpp::Logger<::FlashCpp::LogLevel::level, ::FlashCpp::LogCategory::cat>::levelName() \
                                  << "][" << ::FlashCpp::Logger<::FlashCpp::LogLevel::level, ::FlashCpp::LogCategory::cat>::categoryName() << "] " \
                                  << std::format(fmt, __VA_ARGS__); \
                    if (::FlashCpp::LogConfig::use_colors) { \
                        flash_log_out << ::FlashCpp::detail::RESET; \
                    } \
                    flash_log_out << "\n"; \
                } \
            } \
        } \
    } while(0)

} // namespace FlashCpp
