// Simple test - static member access through full specialization inheritance
template<typename T> 
struct Base { 
    static const int value = 0;
};

template<> 
struct Base<int> { 
    static const int value = 1;
};

// Full specialization with inheritance (not partial)
template<> 
struct Base<const int> : Base<int> { 
};

int main() {
    return Base<const int>::value - 1;
}
