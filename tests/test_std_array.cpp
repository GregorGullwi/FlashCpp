// Test standard <array> header
#include <array>

int main() {
    std::array<int, 5> arr = {1, 2, 3, 4, 5};
    
    return arr[0] == 1 ? 0 : 1;
}
