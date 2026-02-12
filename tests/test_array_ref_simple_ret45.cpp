// Simpler test for references with array subscript

int main() {
    int arr[3] = {10, 20, 30};
    
    // Direct array subscript compound assignment works
    arr[0] += 5;  // 10 + 5 = 15
    
    // Test reference to array element
    int& ref = arr[1];
    ref += 10;  // 20 + 10 = 30
    
    // Result should be arr[0] + arr[1] = 15 + 30 = 45
    return arr[0] + arr[1];
}
