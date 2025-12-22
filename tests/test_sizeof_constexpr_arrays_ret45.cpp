int main() {
    constexpr int size1 = 10;
    constexpr int size2 = 5;
    
    int arr1[size1];
    char arr2[size2];
    
    int s1 = sizeof(arr1);  // Should be 40
    int s2 = sizeof(arr2);  // Should be 5
    
    return s1 + s2;  // Should be 45
}
