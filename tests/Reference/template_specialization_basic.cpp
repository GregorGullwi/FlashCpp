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
    
    return generic + specialized;  // Should return 1
}

