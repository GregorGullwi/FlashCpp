// Test standard <span> header (C++20)
#include <span>

int main() {
    int arr[5] = {1, 2, 3, 4, 5};
    std::span<int> s(arr, 5);
    
    return s[0] == 1 ? 0 : 1;
}
