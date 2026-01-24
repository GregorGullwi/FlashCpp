// Test: Member function requires clause in trailing specifiers
// This tests requires clauses that appear after the function signature
// Pattern: ReturnType func(params) const requires constraint { body }

template<typename T>
concept HasSize = requires(T t) { sizeof(t); };

template<typename T>
concept SmallType = HasSize<T> && sizeof(T) <= 4;

// Test 1: Member function with trailing requires clause
template<typename T>
class Container {
public:
    T value;
    
    Container(T v) : value(v) {}
    
    // Member function with requires in trailing position
    int getSize() const requires HasSize<T> {
        return sizeof(T);
    }
    
    // Another member function with requires after const
    T getValue() const requires SmallType<T> {
        return value;
    }
};

// Test 2: Multiple overloads with different requires clauses
template<typename T>
class Processor {
public:
    T data;
    
    Processor(T d) : data(d) {}
    
    // Overload 1: For small types
    int process() const requires SmallType<T> {
        return sizeof(T) * 10;
    }
    
    // Can have regular member functions too
    T get() const {
        return data;
    }
};

int main() {
    Container<int> c(10);
    int size = c.getSize();  // sizeof(int) = 4
    int val = c.getValue();  // 10
    
    Processor<char> p(7);
    int processed = p.process();  // sizeof(char) * 10 = 10
    
    // Return: 4 + 10 + 10 + 18 = 42
    return size + val + processed + 18;
}
