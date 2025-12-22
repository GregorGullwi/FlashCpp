int main() {
    int arr1[10];
    char arr2[5];
    long arr3[3];
    
    int s1 = sizeof(arr1);  // Should be 40 (10 * 4)
    int s2 = sizeof(arr2);  // Should be 5 (5 * 1)
    int s3 = sizeof(arr3);  // Should be 24 (3 * 8)
    
    // Return total: 40 + 5 + 24 = 69
    return s1 + s2 + s3;
}
