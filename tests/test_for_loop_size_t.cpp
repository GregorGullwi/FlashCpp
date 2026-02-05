// Test for loop with size_t - verifies that size_t is recognized as a type in for-loop init
#include <cstddef>

int sum_array(const int* arr, size_t length) {
    int sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += arr[i];
    }
    return sum;
}

int main() {
    int numbers[] = {1, 2, 3, 4, 5};
    size_t count = 5;
    return sum_array(numbers, count);
}
