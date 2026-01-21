#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <climits>
#include <cstdint>
#include "StringTable.h"

// Hash functor for StringHandle to use in unordered containers
struct StringHandleHash {
    size_t operator()(const StringHandle& h) const noexcept {
        return h.hash();
    }
};

// Define FLASHCPP_LOG_LEVEL if not already defined (e.g., by Log.h or build flags)
// This allows this header to be included independently
#ifndef FLASHCPP_LOG_LEVEL
    #ifdef NDEBUG
        #define FLASHCPP_LOG_LEVEL 2
    #else
        #define FLASHCPP_LOG_LEVEL 4
    #endif
#endif

// Compile-time flag to enable/disable template profiling
// Set to 1 to enable profiling, 0 to disable for production builds
#ifndef ENABLE_TEMPLATE_PROFILING
#define ENABLE_TEMPLATE_PROFILING 1
#endif

// Enable detailed instantiation tracking only at info level or higher
// This controls the tracking of current instantiation depth and name
#ifndef ENABLE_TEMPLATE_INSTANTIATION_TRACKING
    #if FLASHCPP_LOG_LEVEL >= 2
        #define ENABLE_TEMPLATE_INSTANTIATION_TRACKING 1
    #else
        #define ENABLE_TEMPLATE_INSTANTIATION_TRACKING 0
    #endif
#endif

#if ENABLE_TEMPLATE_PROFILING

// Accumulator for profiling multiple operations of the same type
class TemplateProfilingAccumulator {
public:
    TemplateProfilingAccumulator()
        : count_(0), total_duration_(0),
          min_duration_(INT64_MAX), max_duration_(INT64_MIN) {}

    void add(std::chrono::microseconds duration) {
        int64_t dur = duration.count();
        total_duration_ += dur;
        ++count_;

        // Track min/max
        if (dur < min_duration_) min_duration_ = dur;
        if (dur > max_duration_) max_duration_ = dur;
    }

    size_t count() const { return count_; }
    int64_t total_duration() const { return total_duration_; }
    int64_t min_duration() const { return min_duration_ == INT64_MAX ? 0 : min_duration_; }
    int64_t max_duration() const { return max_duration_ == INT64_MIN ? 0 : max_duration_; }
    double mean_duration() const {
        return count_ > 0 ? static_cast<double>(total_duration_) / count_ : 0.0;
    }

private:
    size_t count_;
    int64_t total_duration_;  // in microseconds
    int64_t min_duration_;    // minimum duration in microseconds
    int64_t max_duration_;    // maximum duration in microseconds
};

// Global template profiling statistics
class TemplateProfilingStats {
public:
    static TemplateProfilingStats& getInstance() {
        static TemplateProfilingStats instance;
        return instance;
    }

    // Record a template instantiation timing using StringHandle for efficiency
    // The template name is stored by index and only looked up when printing
    void recordInstantiation(StringHandle template_name_handle, std::chrono::microseconds duration) {
        instantiations_by_handle_[template_name_handle].add(duration);
        incrementInstantiationCount();
        
        // Log progress every 100 instantiations when Info level is enabled
        // This helps track where the compiler gets stuck during template-heavy compilations
        #if ENABLE_TEMPLATE_INSTANTIATION_TRACKING
        // Record in interval and total stats
        recordInstantiationTime(duration.count());
        maybeLogProgress(50);  // Increased frequency: log every 50 instantiations instead of 100
        #endif
    }
    
    // Overload for std::string for backward compatibility
    void recordInstantiation(const std::string& template_name, std::chrono::microseconds duration) {
        StringHandle handle = StringTable::getOrInternStringHandle(template_name);
        recordInstantiation(handle, duration);
    }

#if ENABLE_TEMPLATE_INSTANTIATION_TRACKING
    // Track template instantiation start (for detecting stuck instantiations)
    void recordInstantiationStart(StringHandle template_name) {
        current_instantiation_ = template_name;
        current_instantiation_start_ = std::chrono::high_resolution_clock::now();
        ++instantiation_depth_;
    }

    // Track template instantiation end
    void recordInstantiationEnd(StringHandle /*template_name*/) {
        if (instantiation_depth_ > 0) {
            --instantiation_depth_;
        }
        if (instantiation_depth_ == 0) {
            current_instantiation_ = StringHandle();
        }
    }

    // Get the current template being instantiated (for debugging)
    StringHandle getCurrentInstantiation() const {
        return current_instantiation_;
    }

    // Get current instantiation depth
    size_t getInstantiationDepth() const {
        return instantiation_depth_;
    }
#endif

    // Record a cache hit using StringHandle for efficiency
    void recordCacheHit(StringHandle template_name_handle) {
        cache_hits_by_handle_[template_name_handle]++;
    }
    
    // Overload for std::string for backward compatibility
    void recordCacheHit(const std::string& template_name) {
        StringHandle handle = StringTable::getOrInternStringHandle(template_name);
        recordCacheHit(handle);
    }

    // Record a cache miss using StringHandle for efficiency
    void recordCacheMiss(StringHandle template_name_handle) {
        cache_misses_by_handle_[template_name_handle]++;
    }
    
    // Overload for std::string for backward compatibility
    void recordCacheMiss(const std::string& template_name) {
        StringHandle handle = StringTable::getOrInternStringHandle(template_name);
        recordCacheMiss(handle);
    }

    // Record template lookup time
    void recordLookup(std::chrono::microseconds duration) {
        lookup_time_.add(duration);
    }

    // Record template parsing time
    void recordParsing(std::chrono::microseconds duration) {
        parsing_time_.add(duration);
    }

    // Record type substitution time
    void recordSubstitution(std::chrono::microseconds duration) {
        substitution_time_.add(duration);
    }

    // Record specialization matching time
    void recordSpecializationMatch(std::chrono::microseconds duration) {
        specialization_match_time_.add(duration);
    }

    // Print comprehensive statistics
    void printStats() const {
        printf("\n=== Template Instantiation Profiling ===\n\n");

        // Overall statistics
        printf("Overall Breakdown:\n");
        printf("  %-30s: count=%5zu, total=%8.3f ms, mean=%8.3f μs, min=%8lld μs, max=%8lld μs\n",
               "Template Lookups", lookup_time_.count(), lookup_time_.total_duration() / 1000.0,
               lookup_time_.mean_duration(), (long long)lookup_time_.min_duration(), (long long)lookup_time_.max_duration());
        printf("  %-30s: count=%5zu, total=%8.3f ms, mean=%8.3f μs, min=%8lld μs, max=%8lld μs\n",
               "Template Parsing", parsing_time_.count(), parsing_time_.total_duration() / 1000.0,
               parsing_time_.mean_duration(), (long long)parsing_time_.min_duration(), (long long)parsing_time_.max_duration());
        printf("  %-30s: count=%5zu, total=%8.3f ms, mean=%8.3f μs, min=%8lld μs, max=%8lld μs\n",
               "Type Substitution", substitution_time_.count(), substitution_time_.total_duration() / 1000.0,
               substitution_time_.mean_duration(), (long long)substitution_time_.min_duration(), (long long)substitution_time_.max_duration());
        printf("  %-30s: count=%5zu, total=%8.3f ms, mean=%8.3f μs, min=%8lld μs, max=%8lld μs\n",
               "Specialization Matching", specialization_match_time_.count(), 
               specialization_match_time_.total_duration() / 1000.0,
               specialization_match_time_.mean_duration(), (long long)specialization_match_time_.min_duration(), 
               (long long)specialization_match_time_.max_duration());

        printf("\nCache Statistics:\n");
        size_t total_hits = 0, total_misses = 0;
        for (const auto& [handle, hits] : cache_hits_by_handle_) {
            total_hits += hits;
        }
        for (const auto& [handle, misses] : cache_misses_by_handle_) {
            total_misses += misses;
        }
        size_t total_requests = total_hits + total_misses;
        double hit_rate = total_requests > 0 ? (100.0 * total_hits / total_requests) : 0.0;
        printf("  Cache Hits:   %zu\n", total_hits);
        printf("  Cache Misses: %zu\n", total_misses);
        printf("  Hit Rate:     %.2f%%\n", hit_rate);

        // Top 10 most instantiated templates (lookup names only when printing)
        if (!instantiations_by_handle_.empty()) {
            printf("\nTop 10 Most Instantiated Templates (by count):\n");
            std::vector<std::pair<StringHandle, const TemplateProfilingAccumulator*>> sorted;
            for (const auto& [handle, acc] : instantiations_by_handle_) {
                sorted.push_back({handle, &acc});
            }
            std::sort(sorted.begin(), sorted.end(),
                     [](const auto& a, const auto& b) { return a.second->count() > b.second->count(); });

            for (size_t i = 0; i < (std::min)(size_t(10), sorted.size()); ++i) {
                const auto& [handle, acc] = sorted[i];
                // Only lookup the name here when printing
                std::string_view name = StringTable::getStringView(handle);
                printf("  %2zu. %-40.*s: count=%5zu, total=%8.3f ms, mean=%8.3f μs\n",
                       i + 1, (int)std::min(name.size(), size_t(40)), name.data(),
                       acc->count(), acc->total_duration() / 1000.0, acc->mean_duration());
            }
        }

        // Top 10 slowest templates (lookup names only when printing)
        if (!instantiations_by_handle_.empty()) {
            printf("\nTop 10 Slowest Templates (by total time):\n");
            std::vector<std::pair<StringHandle, const TemplateProfilingAccumulator*>> sorted;
            for (const auto& [handle, acc] : instantiations_by_handle_) {
                sorted.push_back({handle, &acc});
            }
            std::sort(sorted.begin(), sorted.end(),
                     [](const auto& a, const auto& b) { 
                         return a.second->total_duration() > b.second->total_duration(); 
                     });

            for (size_t i = 0; i < (std::min)(size_t(10), sorted.size()); ++i) {
                const auto& [handle, acc] = sorted[i];
                // Only lookup the name here when printing
                std::string_view name = StringTable::getStringView(handle);
                printf("  %2zu. %-40.*s: count=%5zu, total=%8.3f ms, mean=%8.3f μs\n",
                       i + 1, (int)std::min(name.size(), size_t(40)), name.data(),
                       acc->count(), acc->total_duration() / 1000.0, acc->mean_duration());
            }
        }

        printf("\n=== End Template Profiling ===\n\n");
    }

    // Reset all statistics
    void reset() {
        instantiations_by_handle_.clear();
        cache_hits_by_handle_.clear();
        cache_misses_by_handle_.clear();
        lookup_time_ = TemplateProfilingAccumulator();
        parsing_time_ = TemplateProfilingAccumulator();
        substitution_time_ = TemplateProfilingAccumulator();
        specialization_match_time_ = TemplateProfilingAccumulator();
        total_instantiation_count_ = 0;
#if ENABLE_TEMPLATE_INSTANTIATION_TRACKING
        last_progress_count_ = 0;
#endif
        start_time_ = std::chrono::high_resolution_clock::now();
    }

    // Get total instantiation count across all templates
    size_t getTotalInstantiationCount() const {
        return total_instantiation_count_;
    }

#if ENABLE_TEMPLATE_INSTANTIATION_TRACKING
    // Record individual instantiation time for both interval and total statistics.
    // This is called from recordInstantiation() to track timing data that gets
    // printed in periodic progress reports.
    // @param duration_us The duration of the instantiation in microseconds
    void recordInstantiationTime(int64_t duration_us) {
        interval_stats_.add(std::chrono::microseconds(duration_us));
        total_stats_.add(std::chrono::microseconds(duration_us));
    }

    // Check if progress should be logged (every N instantiations)
    // Returns true if progress was logged
    bool maybeLogProgress(size_t interval = 1000) {
        if (total_instantiation_count_ - last_progress_count_ >= interval) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
            auto interval_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - interval_start_time_).count();
            
            size_t total_hits = 0, total_misses = 0;
            for (const auto& [handle, hits] : cache_hits_by_handle_) {
                total_hits += hits;
            }
            for (const auto& [handle, misses] : cache_misses_by_handle_) {
                total_misses += misses;
            }
            size_t total_requests = total_hits + total_misses;
            double hit_rate = total_requests > 0 ? (100.0 * total_hits / total_requests) : 0.0;
            
            // Calculate templates per second for this interval
            size_t interval_count = total_instantiation_count_ - last_progress_count_;
            double templates_per_sec = interval_elapsed_ms > 0 ? (interval_count * 1000.0 / interval_elapsed_ms) : 0.0;
            
            // Print interval statistics
            printf("[Progress] %zu templates in %lld ms total (%.0f/sec) | Interval: min=%.0fμs avg=%.0fμs max=%.0fμs | Total: min=%.0fμs avg=%.0fμs max=%.0fμs | cache=%.1f%%",
                   total_instantiation_count_, (long long)elapsed_ms, templates_per_sec,
                   (double)interval_stats_.min_duration(), interval_stats_.mean_duration(), (double)interval_stats_.max_duration(),
                   (double)total_stats_.min_duration(), total_stats_.mean_duration(), (double)total_stats_.max_duration(),
                   hit_rate);
            
            if (current_instantiation_.isValid() && instantiation_depth_ > 0) {
                std::string_view current_name = StringTable::getStringView(current_instantiation_);
                // Truncate long names (max_len - 3 for "...")
                constexpr size_t max_len = 40;
                constexpr size_t truncate_len = max_len - 3;
                if (current_name.size() > max_len) {
                    printf(" depth=%zu current=%.*s...\n", instantiation_depth_, (int)truncate_len, current_name.data());
                } else {
                    printf(" depth=%zu current=%.*s\n", instantiation_depth_, (int)current_name.size(), current_name.data());
                }
            } else {
                printf("\n");
            }
            fflush(stdout);
            
            // Reset interval stats but keep total stats
            interval_stats_ = TemplateProfilingAccumulator();
            interval_start_time_ = now;
            last_progress_count_ = total_instantiation_count_;
            return true;
        }
        return false;
    }
#endif

    // Increment total instantiation count (called by recordInstantiation)
    void incrementInstantiationCount() {
        ++total_instantiation_count_;
    }

private:
#if ENABLE_TEMPLATE_INSTANTIATION_TRACKING
    TemplateProfilingStats() : total_instantiation_count_(0), last_progress_count_(0),
                               instantiation_depth_(0),
                               current_instantiation_start_(std::chrono::high_resolution_clock::now()),
                               interval_start_time_(std::chrono::high_resolution_clock::now()),
                               start_time_(std::chrono::high_resolution_clock::now()) {}
#else
    TemplateProfilingStats() : total_instantiation_count_(0),
                               start_time_(std::chrono::high_resolution_clock::now()) {}
#endif

    // Use StringHandle-based maps for efficiency (defer name lookup until printing)
    std::unordered_map<StringHandle, TemplateProfilingAccumulator, StringHandleHash> instantiations_by_handle_;
    std::unordered_map<StringHandle, size_t, StringHandleHash> cache_hits_by_handle_;
    std::unordered_map<StringHandle, size_t, StringHandleHash> cache_misses_by_handle_;
    size_t total_instantiation_count_;
#if ENABLE_TEMPLATE_INSTANTIATION_TRACKING
    size_t last_progress_count_;
    size_t instantiation_depth_;
    StringHandle current_instantiation_;
    std::chrono::high_resolution_clock::time_point current_instantiation_start_;
    std::chrono::high_resolution_clock::time_point interval_start_time_;
    TemplateProfilingAccumulator interval_stats_;  // Reset every interval print
    TemplateProfilingAccumulator total_stats_;     // Never reset
#endif
    std::chrono::high_resolution_clock::time_point start_time_;
    TemplateProfilingAccumulator lookup_time_;
    TemplateProfilingAccumulator parsing_time_;
    TemplateProfilingAccumulator substitution_time_;
    TemplateProfilingAccumulator specialization_match_time_;
};

// RAII timer for automatic timing of template operations
class TemplateProfilingTimer {
public:
    enum class Operation {
        Instantiation,
        Lookup,
        Parsing,
        Substitution,
        SpecializationMatch
    };

    TemplateProfilingTimer(Operation op, const std::string& name = "")
        : operation_(op), name_(name), start_(std::chrono::high_resolution_clock::now()) {
        // Log start of instantiation for long-running template instantiation debugging
        #if ENABLE_TEMPLATE_INSTANTIATION_TRACKING
        if (operation_ == Operation::Instantiation && !name_.empty()) {
            name_handle_ = StringTable::getOrInternStringHandle(name_);
            TemplateProfilingStats::getInstance().recordInstantiationStart(name_handle_);
        }
        #endif
    }

    ~TemplateProfilingTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);

        auto& stats = TemplateProfilingStats::getInstance();
        switch (operation_) {
            case Operation::Instantiation:
                #if ENABLE_TEMPLATE_INSTANTIATION_TRACKING
                stats.recordInstantiationEnd(name_handle_);
                #endif
                stats.recordInstantiation(name_, duration);
                break;
            case Operation::Lookup:
                stats.recordLookup(duration);
                break;
            case Operation::Parsing:
                stats.recordParsing(duration);
                break;
            case Operation::Substitution:
                stats.recordSubstitution(duration);
                break;
            case Operation::SpecializationMatch:
                stats.recordSpecializationMatch(duration);
                break;
        }
    }

private:
    Operation operation_;
    std::string name_;
#if ENABLE_TEMPLATE_INSTANTIATION_TRACKING
    StringHandle name_handle_;
#endif
    std::chrono::high_resolution_clock::time_point start_;
};

// Convenience macros for profiling
// Use __COUNTER__ to generate unique variable names, preventing shadowing warnings
#define PROFILE_TEMPLATE_CONCAT_IMPL(a, b) a##b
#define PROFILE_TEMPLATE_CONCAT(a, b) PROFILE_TEMPLATE_CONCAT_IMPL(a, b)
#define PROFILE_TEMPLATE_UNIQUE_VAR PROFILE_TEMPLATE_CONCAT(_template_prof_timer_, __COUNTER__)

#define PROFILE_TEMPLATE_INSTANTIATION(name) \
    TemplateProfilingTimer PROFILE_TEMPLATE_UNIQUE_VAR(TemplateProfilingTimer::Operation::Instantiation, name)

#define PROFILE_TEMPLATE_LOOKUP() \
    TemplateProfilingTimer PROFILE_TEMPLATE_UNIQUE_VAR(TemplateProfilingTimer::Operation::Lookup)

#define PROFILE_TEMPLATE_PARSING() \
    TemplateProfilingTimer PROFILE_TEMPLATE_UNIQUE_VAR(TemplateProfilingTimer::Operation::Parsing)

#define PROFILE_TEMPLATE_SUBSTITUTION() \
    TemplateProfilingTimer PROFILE_TEMPLATE_UNIQUE_VAR(TemplateProfilingTimer::Operation::Substitution)

#define PROFILE_TEMPLATE_SPECIALIZATION_MATCH() \
    TemplateProfilingTimer PROFILE_TEMPLATE_UNIQUE_VAR(TemplateProfilingTimer::Operation::SpecializationMatch)

#define PROFILE_TEMPLATE_CACHE_HIT(name) \
    TemplateProfilingStats::getInstance().recordCacheHit(name)

#define PROFILE_TEMPLATE_CACHE_MISS(name) \
    TemplateProfilingStats::getInstance().recordCacheMiss(name)

#else

// Empty implementations when profiling is disabled
class TemplateProfilingStats {
public:
    static TemplateProfilingStats& getInstance() {
        static TemplateProfilingStats instance;
        return instance;
    }
    void printStats() const {}
    void reset() {}
};

#define PROFILE_TEMPLATE_INSTANTIATION(name) do {} while(0)
#define PROFILE_TEMPLATE_LOOKUP() do {} while(0)
#define PROFILE_TEMPLATE_PARSING() do {} while(0)
#define PROFILE_TEMPLATE_SUBSTITUTION() do {} while(0)
#define PROFILE_TEMPLATE_SPECIALIZATION_MATCH() do {} while(0)
#define PROFILE_TEMPLATE_CACHE_HIT(name) do {} while(0)
#define PROFILE_TEMPLATE_CACHE_MISS(name) do {} while(0)

#endif // ENABLE_TEMPLATE_PROFILING
