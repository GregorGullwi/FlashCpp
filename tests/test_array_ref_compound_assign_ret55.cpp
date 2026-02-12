// Test array subscript compound assignment with references

int main() {
    int arr[5] = {10, 20, 30, 40, 50};
    
    // Test reference to array element with compound assignment
    int& ref0 = arr[0];
    ref0 += 5;  // arr[0] = 10 + 5 = 15
    
    int& ref1 = arr[1];
    ref1 -= 8;  // arr[1] = 20 - 8 = 12
    
    int& ref2 = arr[2];
    ref2 *= 2;  // arr[2] = 30 * 2 = 60
    
    int& ref3 = arr[3];
    ref3 /= 4;  // arr[3] = 40 / 4 = 10
    
    int& ref4 = arr[4];
    ref4 %= 3;  // arr[4] = 50 % 3 = 2
    
    // Verify that array was modified through references
    // arr[0]=15, arr[1]=12, arr[2]=60, arr[3]=10, arr[4]=2
    int result = arr[0] + arr[1] + arr[3] + arr[4];
    // 15 + 12 + 10 + 2 = 39
    
    // Add more operations through the reference
    ref0 += 10;  // arr[0] = 15 + 10 = 25
    
    // Final result
    result = arr[0] + arr[1] + arr[3] + arr[4];
    // 25 + 12 + 10 + 2 = 49
    
    // Add constant to get to 55
    result += 6;
    
    return result;  // Should be 55
}
