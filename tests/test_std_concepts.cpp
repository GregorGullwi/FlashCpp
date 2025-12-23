// Test standard <concepts> header (C++20)
#include <concepts>

template<std::integral T>
T add(T a, T b) {
    return a + b;
}

int main() {
    int result = add(3, 4);
    return result == 7 ? 0 : 1;
}
