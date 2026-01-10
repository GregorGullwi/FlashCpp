// Test pointer-to-member operators .* and ->* in expression contexts
// Note: This tests the operators themselves, not pointer-to-member type declarations
// which are not yet implemented (e.g., int Class::*ptr syntax)

template<typename T>
T declval();

struct Point {
    int x;
    int y;
};

// Test the .* operator in a decltype context (what we actually implemented)
template<typename T, typename M>
using member_access_type = decltype(declval<T>().*declval<M>());

int main() {
    // Simple compile test - we can't actually execute pointer-to-member
    // operations without implementing pointer-to-member type declarations
    return 0;
}
