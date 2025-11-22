// Test std::array constexpr support
#include <array>

// Basic initialization and access
constexpr int test_basic_array() {
    std::array<int, 3> arr = {10, 20, 30};
    return arr[0] + arr[1] + arr[2];
}
static_assert(test_basic_array() == 60, "Basic array test should return 60");

// Size query
constexpr int test_array_size() {
    std::array<int, 5> arr = {1, 2, 3, 4, 5};
    return arr.size();
}
static_assert(test_array_size() == 5, "Array size should be 5");

// Element access
constexpr int test_array_access() {
    std::array<int, 4> arr = {100, 200, 300, 400};
    return arr[2];
}
static_assert(test_array_access() == 300, "Should access third element");

// Front and back
constexpr int test_front_back() {
    std::array<int, 3> arr = {5, 10, 15};
    return arr.front() + arr.back();
}
static_assert(test_front_back() == 20, "Front (5) + back (15) = 20");

// Modification
constexpr int test_modification() {
    std::array<int, 3> arr = {1, 2, 3};
    arr[1] = 99;
    return arr[0] + arr[1] + arr[2];
}
static_assert(test_modification() == 103, "1 + 99 + 3 = 103");

int main() {
    return 0;
}
