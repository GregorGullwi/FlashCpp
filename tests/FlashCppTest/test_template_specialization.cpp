// Test cases for template specialization

// Test 1: Basic full specialization
template<typename T>
class Container {
public:
    int getType() { return 0; }
};

template<>
class Container<int> {
public:
    int getType() { return 1; }
};

// Test 2: Specialization with different members
template<typename T>
class Storage {
public:
    T value;
    int size() { return sizeof(T); }
};

template<>
class Storage<bool> {
public:
    bool value;
    int size() { return 1; }  // Optimized for bool
};

// Test 3: Specialization with multiple member functions
template<typename T>
class Calculator {
public:
    T add(T a, T b) { return a + b; }
    T multiply(T a, T b) { return a * b; }
};

template<>
class Calculator<int> {
public:
    int add(int a, int b) { return a + b + 1; }  // Different implementation
    int multiply(int a, int b) { return a * b * 2; }  // Different implementation
};

// Test 4: Specialization returning different type
template<typename T>
class TypeId {
public:
    int getId() { return 0; }
};

template<>
class TypeId<float> {
public:
    int getId() { return 100; }
};

template<>
class TypeId<char> {
public:
    int getId() { return 200; }
};

int main() {
    // Test 1: Basic specialization
    Container<int> c1;
    int result1 = c1.getType();  // Should return 1
    
    Container<char> c2;
    int result2 = c2.getType();  // Should return 0 (primary template)
    
    // Test 2: Different members
    Storage<int> s1;
    int size1 = s1.size();  // Should return 4 (sizeof(int))
    
    Storage<bool> s2;
    int size2 = s2.size();  // Should return 1 (specialized)
    
    // Test 3: Multiple functions with template types
    Calculator<int> calc1;
    int sum = calc1.add(5, 3);  // Should return 9 (5+3+1)
    int product = calc1.multiply(4, 2);  // Should return 16 (4*2*2)
    
    Calculator<float> calc2;
    float sum2 = calc2.add(5.0f, 3.0f);  // Should return 8.0 (primary template)
    
    // Test 4: Multiple specializations
    TypeId<int> t1;
    int id1 = t1.getId();  // Should return 0
    
    TypeId<float> t2;
    int id2 = t2.getId();  // Should return 100
    
    TypeId<char> t3;
    int id3 = t3.getId();  // Should return 200
    
    // Return combined result for verification
    return result1 + result2 + size2 + id2 + id3;  // 1 + 0 + 1 + 100 + 200 = 302
}
