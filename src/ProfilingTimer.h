#pragma once

#include <chrono>
#include <string>
#include <iostream>
#include <cstdint>
#include <climits>

class ProfilingTimer {
public:
    ProfilingTimer(const std::string& name, bool enabled = true)
        : name_(name), enabled_(enabled), start_(std::chrono::high_resolution_clock::now()) {
    }

    ~ProfilingTimer() {
        if (enabled_) {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
            std::cout << name_ << ": " << duration.count() << " μs" << std::endl;
        }
    }

private:
    std::string name_;
    bool enabled_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

// Accumulator for profiling multiple operations of the same type
// Lightweight version without median (no dynamic allocation)
class ProfilingAccumulator {
public:
    ProfilingAccumulator(const std::string& name)
        : name_(name), count_(0), total_duration_(0),
          min_duration_(INT64_MAX), max_duration_(INT64_MIN) {}

    void add(std::chrono::microseconds duration) {
        int64_t dur = duration.count();
        total_duration_ += dur;
        ++count_;

        // Track min/max
        if (dur < min_duration_) min_duration_ = dur;
        if (dur > max_duration_) max_duration_ = dur;
    }

    void print() const {
        if (count_ == 0) {
            printf("    %-30s: no samples\n", name_.c_str());
            return;
        }

        double mean = static_cast<double>(total_duration_) / count_;

        printf("    %-30s: count=%5zu, total=%8.3f ms, mean=%8.3f μs, min=%8.3f μs, max=%8.3f μs\n",
               name_.c_str(), count_, total_duration_ / 1000.0, mean,
               min_duration_ / 1.0, max_duration_ / 1.0);
    }

private:
    std::string name_;
    size_t count_;
    int64_t total_duration_;  // in microseconds
    int64_t min_duration_;    // minimum duration in microseconds
    int64_t max_duration_;    // maximum duration in microseconds
};

