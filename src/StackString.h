#pragma once

#include <string>
#include <string_view>
#include <cstring>
#include <algorithm>
#include <memory>
#include <atomic>

// Performance measurement switch
// Set to 1 to use old std::string approach, 0 to use StackString
#ifndef USE_OLD_STRING_APPROACH
#define USE_OLD_STRING_APPROACH 1
#endif

// Performance tracking for StackString
struct StackStringStats {
    static std::atomic<size_t> stack_allocations;
    static std::atomic<size_t> heap_allocations;
    static std::atomic<size_t> total_bytes_on_stack;
    static std::atomic<size_t> total_bytes_on_heap;

    static void reset() {
        stack_allocations = 0;
        heap_allocations = 0;
        total_bytes_on_stack = 0;
        total_bytes_on_heap = 0;
    }

    static void print_stats() {
        size_t stack_allocs = stack_allocations.load();
        size_t heap_allocs = heap_allocations.load();
        size_t stack_bytes = total_bytes_on_stack.load();
        size_t heap_bytes = total_bytes_on_heap.load();
        size_t total_allocs = stack_allocs + heap_allocs;

        printf("\n=== StackString Performance Stats ===\n");
        printf("Stack allocations: %zu (%.1f%%)\n", stack_allocs,
               total_allocs > 0 ? 100.0 * stack_allocs / total_allocs : 0.0);
        printf("Heap allocations:  %zu (%.1f%%)\n", heap_allocs,
               total_allocs > 0 ? 100.0 * heap_allocs / total_allocs : 0.0);
        printf("Total allocations: %zu\n", total_allocs);
        printf("Stack bytes: %zu\n", stack_bytes);
        printf("Heap bytes:  %zu\n", heap_bytes);
        printf("Total bytes: %zu\n", stack_bytes + heap_bytes);
        printf("======================================\n\n");
    }
};

// Initialize static members
inline std::atomic<size_t> StackStringStats::stack_allocations{0};
inline std::atomic<size_t> StackStringStats::heap_allocations{0};
inline std::atomic<size_t> StackStringStats::total_bytes_on_stack{0};
inline std::atomic<size_t> StackStringStats::total_bytes_on_heap{0};

// StackString: A string class that stores small strings on the stack
// and only allocates on the heap for larger strings.
//
// Template parameter N is the maximum size (including null terminator)
// that can be stored on the stack. Typical values: 16, 32, 64.
//
// Benefits:
// - Zero heap allocations for strings <= N-1 characters
// - Compatible with std::string_view
// - Implicit conversion to std::string_view for easy integration
// - Move semantics for efficient transfers
// - Efficient heap storage using raw char* (no duplicate size storage like std::string)
template<size_t N = 32>
class StackString {
public:
    // Default constructor - empty string
    StackString() : size_(0), heap_data_(nullptr) {
        buffer_[0] = '\0';
    }

    // Construct from string_view
    explicit StackString(std::string_view sv) : size_(sv.size()), heap_data_(nullptr) {
        if (size_ < N) {
            // Fits on stack
            std::memcpy(buffer_, sv.data(), size_);
            buffer_[size_] = '\0';
            StackStringStats::stack_allocations++;
            StackStringStats::total_bytes_on_stack += size_;
        } else {
            // Need heap allocation - allocate exactly what we need
            heap_data_ = std::make_unique<char[]>(size_ + 1);
            std::memcpy(heap_data_.get(), sv.data(), size_);
            heap_data_[size_] = '\0';
            StackStringStats::heap_allocations++;
            StackStringStats::total_bytes_on_heap += size_;
        }
    }

    // Construct from C string
    explicit StackString(const char* str) : StackString(std::string_view(str)) {}

    // Construct from std::string
    explicit StackString(const std::string& str) : StackString(std::string_view(str)) {}

    // Copy constructor
    StackString(const StackString& other) : size_(other.size_), heap_data_(nullptr) {
        if (other.heap_data_) {
            heap_data_ = std::make_unique<char[]>(size_ + 1);
            std::memcpy(heap_data_.get(), other.heap_data_.get(), size_ + 1);
        } else {
            std::memcpy(buffer_, other.buffer_, size_ + 1);
        }
    }

    // Move constructor
    StackString(StackString&& other) noexcept
        : size_(other.size_), heap_data_(std::move(other.heap_data_)) {
        if (!heap_data_) {
            std::memcpy(buffer_, other.buffer_, size_ + 1);
        }
        other.size_ = 0;
        other.buffer_[0] = '\0';
    }

    // Copy assignment
    StackString& operator=(const StackString& other) {
        if (this != &other) {
            size_ = other.size_;
            if (other.heap_data_) {
                heap_data_ = std::make_unique<char[]>(size_ + 1);
                std::memcpy(heap_data_.get(), other.heap_data_.get(), size_ + 1);
            } else {
                heap_data_.reset();
                std::memcpy(buffer_, other.buffer_, size_ + 1);
            }
        }
        return *this;
    }

    // Move assignment
    StackString& operator=(StackString&& other) noexcept {
        if (this != &other) {
            size_ = other.size_;
            heap_data_ = std::move(other.heap_data_);
            if (!heap_data_) {
                std::memcpy(buffer_, other.buffer_, size_ + 1);
            }
            other.size_ = 0;
            other.buffer_[0] = '\0';
        }
        return *this;
    }

    // Assign from string_view
    StackString& operator=(std::string_view sv) {
        size_ = sv.size();
        if (size_ < N) {
            heap_data_.reset();
            std::memcpy(buffer_, sv.data(), size_);
            buffer_[size_] = '\0';
        } else {
            heap_data_ = std::make_unique<char[]>(size_ + 1);
            std::memcpy(heap_data_.get(), sv.data(), size_);
            heap_data_[size_] = '\0';
        }
        return *this;
    }

    // Get as string_view
    std::string_view view() const {
        if (heap_data_) {
            return std::string_view(heap_data_.get(), size_);
        }
        return std::string_view(buffer_, size_);
    }

    // Implicit conversion to string_view for easy integration
    operator std::string_view() const {
        return view();
    }

    // Get C string
    const char* c_str() const {
        return heap_data_ ? heap_data_.get() : buffer_;
    }

    // Get data pointer
    const char* data() const {
        return heap_data_ ? heap_data_.get() : buffer_;
    }

    // Size
    size_t size() const { return size_; }
    size_t length() const { return size_; }
    bool empty() const { return size_ == 0; }

    // Check if stored on stack (useful for debugging/profiling)
    bool is_on_stack() const { return !heap_data_; }

    // Comparison operators
    bool operator==(const StackString& other) const {
        return view() == other.view();
    }

    bool operator!=(const StackString& other) const {
        return view() != other.view();
    }

    bool operator==(std::string_view sv) const {
        return view() == sv;
    }

    bool operator!=(std::string_view sv) const {
        return view() != sv;
    }

    bool operator<(const StackString& other) const {
        return view() < other.view();
    }

    // For range-based for loops
    const char* begin() const { return data(); }
    const char* end() const { return data() + size_; }

    // Append operations (for building strings)
    StackString& operator+=(std::string_view sv) {
        size_t new_size = size_ + sv.size();

        if (new_size < N && !heap_data_) {
            // Can still fit on stack
            std::memcpy(buffer_ + size_, sv.data(), sv.size());
            size_ = new_size;
            buffer_[size_] = '\0';
        } else {
            // Need to move to heap or append to existing heap data
            auto new_data = std::make_unique<char[]>(new_size + 1);

            if (heap_data_) {
                // Already on heap - copy old heap data
                std::memcpy(new_data.get(), heap_data_.get(), size_);
            } else {
                // Move from stack to heap
                std::memcpy(new_data.get(), buffer_, size_);
            }

            // Append new data
            std::memcpy(new_data.get() + size_, sv.data(), sv.size());
            new_data[new_size] = '\0';

            heap_data_ = std::move(new_data);
            size_ = new_size;
        }
        return *this;
    }

    StackString& operator+=(char c) {
        return *this += std::string_view(&c, 1);
    }

    // Clear the string
    void clear() {
        heap_data_.reset();
        size_ = 0;
        buffer_[0] = '\0';
    }

private:
    char buffer_[N];                      // Stack storage
    size_t size_;                         // Current size (not including null terminator)
    std::unique_ptr<char[]> heap_data_;   // Heap storage for large strings (nullptr if on stack)
};

// Hash support for use in unordered_map/unordered_set
namespace std {
    template<size_t N>
    struct hash<StackString<N>> {
        size_t operator()(const StackString<N>& s) const {
            return hash<string_view>{}(s.view());
        }
    };
}

// Conditional type alias for performance comparison
#if USE_OLD_STRING_APPROACH
    // Use std::string for comparison
    template<size_t N = 32>
    using StringType = std::string;
#else
    // Use StackString (optimized version)
    template<size_t N = 32>
    using StringType = StackString<N>;
#endif

