// Test type trait intrinsic in template argument context
// This tests __has_trivial_destructor(T) being used as a template argument
// Expected return value: 42

template<bool>
struct bool_constant {
    static constexpr bool value = true;
};

template<>
struct bool_constant<false> {
    static constexpr bool value = false;
};

// Test using a type trait intrinsic as template argument  
template<typename T>
struct is_trivially_destructible : bool_constant<__has_trivial_destructor(T)> {
};

struct Trivial {
    int x;
};

struct NonTrivial {
    ~NonTrivial() { }
};

int main() {
    // Trivial type should have trivial destructor (value = true)
    bool b1 = is_trivially_destructible<Trivial>::value;
    
    // NonTrivial type should not have trivial destructor (value = false)
    bool b2 = is_trivially_destructible<NonTrivial>::value;
    
    // Return 42 if both checks pass (b1 == true, b2 == false)
    if (b1 && !b2) {
        return 42;
    }
    return 1;
}
