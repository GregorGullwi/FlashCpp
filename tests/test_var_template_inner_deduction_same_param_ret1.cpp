// Test variable template inner deduction: same parameter used twice must be consistent
// Pattern: v<pair<T, T>> requires both inner args to be the same type
template<typename T, typename U>
struct pair { T first; U second; };
template<typename T>
constexpr int v = 0;
// Both inner args bind to the same T — requires both to be the same type
template<typename T>
constexpr int v<pair<T, T>> = 1;
int main() {
    return v<pair<int, int>>;  // both are int, should match, return 1
}
