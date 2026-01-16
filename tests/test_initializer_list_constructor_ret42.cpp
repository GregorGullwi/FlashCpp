// Test for struct constructor with pointer-like member access
// Tests the infrastructure needed for std::initializer_list support

// Simple wrapper that holds pointer and size (like initializer_list)
class IntRange {
public:
    const int* ptr_;
    unsigned long long size_;
    
    IntRange(const int* p, unsigned long long s) : ptr_(p), size_(s) {}
    
    unsigned long long size() const { return size_; }
    const int* data() const { return ptr_; }
};

class Container {
public:
    int sum;
    
    // Constructor taking IntRange - sums the elements
    Container(IntRange range) : sum(0) {
        const int* p = range.data();
        unsigned long long s = range.size();
        // Sum first 3 elements directly
        if (s >= 1) sum = sum + p[0];
        if (s >= 2) sum = sum + p[1];
        if (s >= 3) sum = sum + p[2];
    }
};

int main() {
    int arr[3] = {10, 20, 12};
    IntRange range(arr, 3);
    Container c(range);
    
    // sum should be 10 + 20 + 12 = 42
    return c.sum;
}
