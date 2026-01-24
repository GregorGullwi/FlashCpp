// Test: Constructor requires clause before member initializer list
// This tests that requires clauses work correctly on constructors
// Pattern: Constructor(params) requires constraint : member_init { body }

template<typename T>
concept Integral = requires { requires sizeof(T) <= 8; };

template<typename T>
concept Arithmetic = Integral<T> || requires { requires sizeof(T) == 4 || sizeof(T) == 8; };

// Test 1: Requires clause on constructor before member initializer
template<typename T>
class ConstrainedValue {
public:
    T value;
    
    // Constructor with requires clause before member initializer list
    ConstrainedValue(T v) requires Integral<T> : value(v) {}
    
    T get() const { return value; }
};

// Test 2: Multiple constructors with different constraints
template<typename T>
class MultiConstrained {
public:
    T data;
    int flag;
    
    // Constructor 1: For integral types
    MultiConstrained(T val) requires Integral<T> 
        : data(val), flag(1) {}
    
    // Constructor 2: For arithmetic types (would include floating point if fully implemented)
    MultiConstrained(T val, int f) requires Arithmetic<T>
        : data(val), flag(f) {}
};

int main() {
    // Use the constrained constructor
    ConstrainedValue<int> cv(20);
    
    // Use first constructor (integral)
    MultiConstrained<int> mc1(10);
    
    // Use second constructor with explicit flag
    MultiConstrained<int> mc2(12, 2);
    
    // Return: 20 + 10 + 12 = 42
    return cv.get() + mc1.data + mc2.data;
}
