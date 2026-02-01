// Basic template specialization test
// Tests full specialization with different implementation

template<typename T>
class Container {
public:
    T value;
    int getType() {
        return 0;  // Generic type
    }
};

template<typename T>
class Wrapper_int {
public:
    T value;
    int getType() {
        return 5;
    }
};

struct Tiny {
    char c;
};

struct Big {
    int x;
};

template<typename T>
int size_val(T) {
    return sizeof(T);
}

template<typename T>
struct Wrap {
    T value;
};

template<template<typename> class Container>
int instantiate_container_int() {
    Container<int> c{};
    c.value = 2;
    return static_cast<int>(sizeof(c.value)) + c.value;
}

// Full specialization for int
template<>
class Container<int> {
public:
    int value;
    int getType() {
        return 1;  // Specialized for int
    }
};

int main() {
    Container<float> cf;
    int generic = cf.getType();  // Should be 0
    
    Container<int> ci;
    int specialized = ci.getType();  // Should be 1

    Wrapper_int<float> w;
    int wrapper = w.getType();

    int sizes = size_val(Big{});
    int wrap_size = instantiate_container_int<Wrap>();
    
    return generic + specialized + wrapper + sizes + wrap_size - 15;  // Should return 1
}
