// Test for std::initializer_list template class constructor
// Tests that:
// 1. std:: namespace is properly mangled with "St" substitution
// 2. Large struct parameters (>64 bits) are passed by pointer correctly
// 3. Compiler generates backing array and initializer_list struct for brace initialization
// 4. Array-to-pointer decay works correctly in pointer arithmetic expressions (ptr + index)

namespace std {
    template<typename T>
    class initializer_list {
    public:
        const T* data_;
        unsigned long size_;
        
        initializer_list() : data_(nullptr), size_(0) {}
        
        const T* begin() const { return data_; }
        unsigned long size() const { return size_; }
    };
}

class Container {
public:
    int sum_;
    
    Container() : sum_(0) {}
    
    Container(std::initializer_list<int> list) : sum_(0) {
        const int* ptr = list.begin();
        const int* end = ptr + list.size();
        // Sum all elements using a while loop with pointer increment
        while (ptr < end) {
            sum_ = sum_ + *ptr;
            ++ptr;
        }
    }
    
    int get_sum() const { return sum_; }
};

int main() {
    Container c{10, 20, 12};  // Compiler creates backing array and initializer_list
    return c.get_sum();       // Should return 42 (10 + 20 + 12)
}
