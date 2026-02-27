// Test variable template inner deduction: specificity ordering
// pair<int,U> is more specific than pair<T,U> for pair<int,float>
template<typename T, typename U>
struct pair { T first; U second; };
template<typename T>
constexpr int v = 0;
template<typename T, typename U>
constexpr int v<pair<T, U>> = 1;
template<typename U>
constexpr int v<pair<int, U>> = 2;
int main() {
    return v<pair<int, float>>;  // should return 2 (most specific)
}
