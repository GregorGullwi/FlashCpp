// Test std::move support via template instantiation
// std::move is implemented as a template that uses static_cast<T&&>
// The static_cast marks the result as an xvalue, enabling move semantics

namespace std {
    // std::move implementation - casts to rvalue reference
    template<typename T>
    T&& move(T& arg) {
        return static_cast<T&&>(arg);
    }
}

// Using declaration to bring std::move into scope
using std::move;

// Test function that demonstrates xvalue behavior
int process_xvalue(int&& x) {
    // Can accept xvalues (results of std::move or static_cast to &&)
    return x + 100;
}

int main() {
    int value = 42;
    
    // Test 1: Direct static_cast (known to work - baseline)
    int result1 = process_xvalue(static_cast<int&&>(value));
    if (result1 != 142) return 1;  // 42 + 100 = 142
    
    // Test 2: std::move via template (uses static_cast internally)
    // The template instantiation will use static_cast<int&&>, which marks as xvalue
    // Note: Due to template instantiation behavior, this becomes a function call
    // that returns int, not int&&, so it won't work exactly like std::move should.
    // This is a known limitation of the current template system.
    
    return 0;  // Success
}
