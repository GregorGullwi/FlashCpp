// Test standard <algorithm> header
#include <algorithm>

int main() {
    int arr[5] = {5, 2, 8, 1, 9};
    std::sort(arr, arr + 5);
    
    return arr[0] == 1 ? 0 : 1;
}
