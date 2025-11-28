// ===== src/Log.h (header-only) =====

#pragma once
#include <iostream>
#include <string_view>
#include <cstdint>

namespace FlashCpp {

// Log categories - each can be enabled/disabled independently
enum class LogCategory : uint32_t {
    None        = 0,
    Parser      = 1 << 0,   // General parser operations
    Lexer       = 1 << 1,   // Lexer/tokenizer
    Templates   = 1 << 2,   // Template instantiation
    Symbols     = 1 << 3,   // Symbol table operations
    Types       = 1 << 4,   // Type resolution
    Codegen     = 1 << 5,   // Code generation / IR
    Scope       = 1 << 6,   // Scope enter/exit
    Mangling    = 1 << 7,   // Name mangling
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
        #define FLASHCPP_LOG_LEVEL 0   // Release: errors only
    #else
        #define FLASHCPP_LOG_LEVEL 3   // Debug: up to Debug level
    #endif
#endif

#ifndef FLASHCPP_LOG_CATEGORIES
    #define FLASHCPP_LOG_CATEGORIES 0xFFFFFFFF  // All categories by default
#endif

// Runtime filter (can be changed at runtime for enabled levels)
struct LogConfig {
    static inline LogLevel runtimeLevel = static_cast<LogLevel>(FLASHCPP_LOG_LEVEL);
    static inline LogCategory runtimeCategories = static_cast<LogCategory>(FLASHCPP_LOG_CATEGORIES);
    static inline std::ostream* output_stream = &std::cerr;  // Configurable output stream

    static void setLevel(LogLevel level) { runtimeLevel = level; }
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
};

// Core logging function
template<LogLevel Level, LogCategory Category>
struct Logger {
    static constexpr bool enabled =
        (static_cast<uint8_t>(Level) <= FLASHCPP_LOG_LEVEL) &&
        ((static_cast<uint32_t>(Category) & FLASHCPP_LOG_CATEGORIES) != 0);

    template<typename... Args>
    static void log([[maybe_unused]] Args&&... args) {
        if constexpr (enabled) {
            // Runtime check
            if (static_cast<uint8_t>(Level) <= static_cast<uint8_t>(LogConfig::runtimeLevel) &&
                (static_cast<uint32_t>(Category) & static_cast<uint32_t>(LogConfig::runtimeCategories)) != 0) {

                // Print prefix
                *LogConfig::output_stream << "[" << levelName() << "][" << categoryName() << "] ";
                (*LogConfig::output_stream << ... << args);
                *LogConfig::output_stream << "\n";
            }
        }
    }

    static constexpr std::string_view levelName() {
        switch (Level) {
            case LogLevel::Error:   return "ERROR";
            case LogLevel::Warning: return "WARN ";
            case LogLevel::Info:    return "INFO ";
            case LogLevel::Debug:   return "DEBUG";
            case LogLevel::Trace:   return "TRACE";
        }
        return "?????";
    }

    static constexpr std::string_view categoryName() {
        switch (Category) {
            case LogCategory::Parser:    return "Parser";
            case LogCategory::Lexer:     return "Lexer";
            case LogCategory::Templates: return "Templates";
            case LogCategory::Symbols:   return "Symbols";
            case LogCategory::Types:     return "Types";
            case LogCategory::Codegen:   return "Codegen";
            case LogCategory::Scope:     return "Scope";
            case LogCategory::Mangling:  return "Mangling";
            default:                     return "General";
        }
    }
};

// Convenience macros - zero overhead when disabled at compile time
#define FLASH_LOG(cat, level, ...) ::FlashCpp::Logger<::FlashCpp::LogLevel::level, ::FlashCpp::LogCategory::cat>::log(__VA_ARGS__)

} // namespace FlashCpp
