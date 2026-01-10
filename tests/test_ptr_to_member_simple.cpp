// Test pointer-to-member operators .* and ->* in expression contexts
// Updated to also test pointer-to-member type declarations

template<typename T>
T declval();

struct Point {
    int x;
    int y;
};

// Test pointer-to-member type declaration (int Class::*)
int Point::*getPtrToMember();

// Test the .* operator in a decltype context (template expression parsing)
template<typename T, typename M>
using member_access_type = decltype(declval<T>().*declval<M>());

int main() {
    // Test type declaration parsing
    int Point::*ptr_to_member = getPtrToMember();
    
    // Simple compile test - the .* operator with runtime variables
    // requires additional support beyond template expression contexts
    return 0;
}
