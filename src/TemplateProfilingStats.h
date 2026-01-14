#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <climits>
#include <cstdint>

// Compile-time flag to enable/disable template profiling
// Set to 1 to enable profiling, 0 to disable for production builds
#ifndef ENABLE_TEMPLATE_PROFILING
#define ENABLE_TEMPLATE_PROFILING 1
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
        return count_ > 0 ? static_cast<double>(total_duration_) / static_cast<double>(count_) : 0.0;
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

    // Record a template instantiation timing
    void recordInstantiation(const std::string& template_name, std::chrono::microseconds duration) {
        instantiations_[template_name].add(duration);
    }

    // Record a cache hit
    void recordCacheHit(const std::string& template_name) {
        cache_hits_[template_name]++;
    }

    // Record a cache miss
    void recordCacheMiss(const std::string& template_name) {
        cache_misses_[template_name]++;
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
               "Template Lookups", lookup_time_.count(), static_cast<double>(lookup_time_.total_duration()) / 1000.0,
               lookup_time_.mean_duration(), (long long)lookup_time_.min_duration(), (long long)lookup_time_.max_duration());
        printf("  %-30s: count=%5zu, total=%8.3f ms, mean=%8.3f μs, min=%8lld μs, max=%8lld μs\n",
               "Template Parsing", parsing_time_.count(), static_cast<double>(parsing_time_.total_duration()) / 1000.0,
               parsing_time_.mean_duration(), (long long)parsing_time_.min_duration(), (long long)parsing_time_.max_duration());
        printf("  %-30s: count=%5zu, total=%8.3f ms, mean=%8.3f μs, min=%8lld μs, max=%8lld μs\n",
               "Type Substitution", substitution_time_.count(), static_cast<double>(substitution_time_.total_duration()) / 1000.0,
               substitution_time_.mean_duration(), (long long)substitution_time_.min_duration(), (long long)substitution_time_.max_duration());
        printf("  %-30s: count=%5zu, total=%8.3f ms, mean=%8.3f μs, min=%8lld μs, max=%8lld μs\n",
               "Specialization Matching", specialization_match_time_.count(), 
               static_cast<double>(specialization_match_time_.total_duration()) / 1000.0,
               specialization_match_time_.mean_duration(), (long long)specialization_match_time_.min_duration(), 
               (long long)specialization_match_time_.max_duration());

        printf("\nCache Statistics:\n");
        size_t total_hits = 0, total_misses = 0;
        for (const auto& [name, hits] : cache_hits_) {
            total_hits += hits;
        }
        for (const auto& [name, misses] : cache_misses_) {
            total_misses += misses;
        }
        size_t total_requests = total_hits + total_misses;
        double hit_rate = total_requests > 0 ? (100.0 * static_cast<double>(total_hits) / static_cast<double>(total_requests)) : 0.0;
        printf("  Cache Hits:   %zu\n", total_hits);
        printf("  Cache Misses: %zu\n", total_misses);
        printf("  Hit Rate:     %.2f%%\n", hit_rate);

        // Top 10 most instantiated templates
        if (!instantiations_.empty()) {
            printf("\nTop 10 Most Instantiated Templates (by count):\n");
            std::vector<std::pair<std::string, const TemplateProfilingAccumulator*>> sorted;
            for (const auto& [name, acc] : instantiations_) {
                sorted.push_back({name, &acc});
            }
            std::sort(sorted.begin(), sorted.end(),
                     [](const auto& a, const auto& b) { return a.second->count() > b.second->count(); });

            for (size_t i = 0; i < (std::min)(size_t(10), sorted.size()); ++i) {
                const auto& [name, acc] = sorted[i];
                printf("  %2zu. %-40s: count=%5zu, total=%8.3f ms, mean=%8.3f μs\n",
                       i + 1, name.c_str(), acc->count(), static_cast<double>(acc->total_duration()) / 1000.0, acc->mean_duration());
            }
        }

        // Top 10 slowest templates
        if (!instantiations_.empty()) {
            printf("\nTop 10 Slowest Templates (by total time):\n");
            std::vector<std::pair<std::string, const TemplateProfilingAccumulator*>> sorted;
            for (const auto& [name, acc] : instantiations_) {
                sorted.push_back({name, &acc});
            }
            std::sort(sorted.begin(), sorted.end(),
                     [](const auto& a, const auto& b) { 
                         return a.second->total_duration() > b.second->total_duration(); 
                     });

            for (size_t i = 0; i < (std::min)(size_t(10), sorted.size()); ++i) {
                const auto& [name, acc] = sorted[i];
                printf("  %2zu. %-40s: count=%5zu, total=%8.3f ms, mean=%8.3f μs\n",
                       i + 1, name.c_str(), acc->count(), static_cast<double>(acc->total_duration()) / 1000.0, acc->mean_duration());
            }
        }

        printf("\n=== End Template Profiling ===\n\n");
    }

    // Reset all statistics
    void reset() {
        instantiations_.clear();
        cache_hits_.clear();
        cache_misses_.clear();
        lookup_time_ = TemplateProfilingAccumulator();
        parsing_time_ = TemplateProfilingAccumulator();
        substitution_time_ = TemplateProfilingAccumulator();
        specialization_match_time_ = TemplateProfilingAccumulator();
    }

private:
    TemplateProfilingStats() = default;

    std::unordered_map<std::string, TemplateProfilingAccumulator> instantiations_;
    std::unordered_map<std::string, size_t> cache_hits_;
    std::unordered_map<std::string, size_t> cache_misses_;
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
        : operation_(op), name_(name), start_(std::chrono::high_resolution_clock::now()) {}

    ~TemplateProfilingTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);

        auto& stats = TemplateProfilingStats::getInstance();
        switch (operation_) {
            case Operation::Instantiation:
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
    std::chrono::high_resolution_clock::time_point start_;
};

// Convenience macros for profiling
#define PROFILE_TEMPLATE_INSTANTIATION(name) \
    TemplateProfilingTimer _template_prof_timer(TemplateProfilingTimer::Operation::Instantiation, name)

#define PROFILE_TEMPLATE_LOOKUP() \
    TemplateProfilingTimer _template_prof_timer(TemplateProfilingTimer::Operation::Lookup)

#define PROFILE_TEMPLATE_PARSING() \
    TemplateProfilingTimer _template_prof_timer(TemplateProfilingTimer::Operation::Parsing)

#define PROFILE_TEMPLATE_SUBSTITUTION() \
    TemplateProfilingTimer _template_prof_timer(TemplateProfilingTimer::Operation::Substitution)

#define PROFILE_TEMPLATE_SPECIALIZATION_MATCH() \
    TemplateProfilingTimer _template_prof_timer(TemplateProfilingTimer::Operation::SpecializationMatch)

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
