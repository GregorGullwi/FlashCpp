// Test variable template partial specialization with inner template argument deduction
// Pattern: decompose_v<pair<T, U>> should deduce T and U from pair<int, char>
template<typename T, typename U>
struct pair { T first; U second; };

template<typename T>
constexpr int decompose_v = 0;

template<typename T, typename U>
constexpr int decompose_v<pair<T, U>> = sizeof(T) + sizeof(U);

int main() {
    // pair<int, char> => sizeof(int) + sizeof(char) = 4 + 1 = 5
    return decompose_v<pair<int, char>>;
}
