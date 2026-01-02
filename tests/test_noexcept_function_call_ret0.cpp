// Test noexcept operator with function calls
// noexcept(f()) should return true if f() is noexcept, false otherwise

void throwing_function() { }  // Not declared noexcept, may throw

void nonthrowing_function() noexcept { }  // Declared noexcept

int main() {
    // noexcept(throwing_function()) should be false
    constexpr bool b1 = noexcept(throwing_function());
    // noexcept(nonthrowing_function()) should be true  
    constexpr bool b2 = noexcept(nonthrowing_function());
    
    // b1 should be false (0), b2 should be true (1)
    // Return 0 if b1 is false and b2 is true
    return (b1 == false && b2 == true) ? 0 : 1;
}
