// Simple test to verify SFINAE actually fails when it should

template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

// This should FAIL to compile - enable_if<false> has no ::type member
typename enable_if<false, int>::type test_var = 42;

int main() {
    return 0;
}
