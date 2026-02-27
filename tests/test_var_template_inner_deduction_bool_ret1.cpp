// Test variable template inner deduction: primary template vs specialization
// Primary returns 0, specialization for pair<T,U> returns 1
template<typename T, typename U>
struct pair { T first; U second; };

template<typename T>
constexpr int is_pair_v = 0;

template<typename T, typename U>
constexpr int is_pair_v<pair<T, U>> = 1;

int main() {
    int result = 0;
    // pair<int, float> should match the specialization
    result += is_pair_v<pair<int, float>>;  // 1
    // int should use the primary template
    result += is_pair_v<int>;  // 0
    return result;  // Expected: 1
}
