int main() {
    // Test 1: Basic array declaration and access
    int numbers[10];
    numbers[0] = 100;
    numbers[5] = 500;
    int first = numbers[0];
    int middle = numbers[5];
    
    // Test 2: Array with variable index
    int index = 3;
    numbers[index] = 300;
    int value = numbers[index];
    
    // Test 3: Multiple arrays
    int arr1[5];
    int arr2[5];
    arr1[0] = 10;
    arr2[0] = 20;
    int sum = arr1[0] + arr2[0];
    
    // Test 4: Array element in expression
    int result = numbers[0] + numbers[5];
    
    // Test 5: Nested array access (using result as index)
    int idx = 2;
    numbers[idx] = 42;
    int val = numbers[idx];
    
    return result;
}

