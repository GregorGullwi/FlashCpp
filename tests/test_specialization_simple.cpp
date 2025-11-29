// Test template specialization

template<typename T>
class Container {
public:
    int getType() { 
        return 0;  // Primary template
    }
};

// Full specialization for int
template<>
class Container<int> {
public:
    int getType() { 
        return 1;  // Specialized for int
    }
};

int main() {
    Container<double> c1;
    Container<int> c2;
    
    // Should return 0 + 1 = 1
    return c1.getType() + c2.getType();
}
