// Test: C++20 explicit(condition) syntax
// This tests conditional explicit constructors based on a boolean expression
// Pattern used extensively in standard library (std::pair, std::tuple, etc.)

template<typename T>
struct is_integral {
    static constexpr bool value = false;
};

template<>
struct is_integral<int> {
    static constexpr bool value = true;
};

template<>
struct is_integral<long> {
    static constexpr bool value = true;
};

// Test 1: explicit(condition) with compile-time constant
template<typename T>
class ConditionalWrapper {
public:
    // Explicit only if T is integral
    explicit(is_integral<T>::value) ConditionalWrapper(T val) : value_(val) {}
    
    T get() const { return value_; }
    
private:
    T value_;
};

// Test 2: explicit(true) and explicit(false)
class AlwaysExplicit {
public:
    explicit(true) AlwaysExplicit(int x) : val(x) {}
    int val;
};

class NeverExplicit {
public:
    explicit(false) NeverExplicit(int x) : val(x) {}
    int val;
};

int main() {
    // ConditionalWrapper<int> is explicit because is_integral<int>::value is true
    ConditionalWrapper<int> wi(20);
    
    // ConditionalWrapper<double> is not explicit because is_integral<double>::value is false
    ConditionalWrapper<double> wd = 22.0;  // Implicit conversion allowed
    
    // AlwaysExplicit requires explicit construction
    AlwaysExplicit ae(10);
    
    // NeverExplicit allows implicit conversion
    NeverExplicit ne = 32;
    
    // Return: 20 + 22 = 42
    return wi.get() + static_cast<int>(wd.get());
}
