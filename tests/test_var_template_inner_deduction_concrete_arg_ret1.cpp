// Test variable template inner deduction: concrete inner arg (int) with deduced param
// Pattern: v<pair<int, U>> should match pair<int, float> and return 1
template<typename T, typename U>
struct pair { T first; U second; };
template<typename T>
constexpr int v = 0;
// Specialization where first inner arg is concrete (int), second is deduced
template<typename U>
constexpr int v<pair<int, U>> = 1;
int main() {
    return v<pair<int, float>>;  // should match specialization, return 1
}
