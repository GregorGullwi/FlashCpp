// Test variable template inner deduction: same parameter used twice, mismatch should reject
// Pattern: v<pair<T, T>> should NOT match pair<int, char> since int != char
template<typename T, typename U>
struct pair { T first; U second; };
template<typename T>
constexpr int v = 0;
template<typename T>
constexpr int v<pair<T, T>> = 1;
int main() {
    return v<pair<int, char>>;  // int != char, should NOT match pair<T,T>, return 0
}
