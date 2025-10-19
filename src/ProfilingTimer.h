#pragma once

#include <chrono>
#include <string>
#include <iostream>

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

