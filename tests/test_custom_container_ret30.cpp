// Test custom container with begin()/end() methods
// NOTE: Parser currently doesn't support out-of-line member function definitions for non-template structs
// This test documents the intended behavior once parser support is added

// Current workaround: Use arrays directly (as in test_range_for.cpp)
// Future: Will support custom containers with member functions:
//
// struct SimpleContainer {
//     int data[3];
//     int* begin();
//     int* end();
// };
//
// int* SimpleContainer::begin() { return &data[0]; }
// int* SimpleContainer::end() { return &data[3]; }

int main() {
    int arr[3];
    arr[0] = 5;
    arr[1] = 10;
    arr[2] = 15;
    
    int sum = 0;
    for (int x : arr) {
        sum = sum + x;
    }
    
    return sum;  // Expected: 30 (5+10+15)
}
