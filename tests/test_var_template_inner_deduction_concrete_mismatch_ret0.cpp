// Test variable template inner deduction: concrete inner arg mismatch falls back to primary
// Pattern: v<pair<int, U>> should NOT match pair<char, float>
template<typename T, typename U>
struct pair { T first; U second; };
template<typename T>
constexpr int v = 0;
template<typename U>
constexpr int v<pair<int, U>> = 1;
int main() {
    // pair<char, float> should NOT match pair<int, U>, so falls back to primary (0)
    return v<pair<char, float>>;  // expected: 0
}
