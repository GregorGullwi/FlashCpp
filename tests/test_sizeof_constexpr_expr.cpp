int main() {
    constexpr int base = 5;
    constexpr int multiplier = 2;
    constexpr int size = base * multiplier;  // 10
    
    int arr[size];
    int s = sizeof(arr);  // Should be 40
    
    return s;
}
