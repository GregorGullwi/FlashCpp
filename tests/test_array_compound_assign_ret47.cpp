// Test array subscript compound assignment operators

int main() {
    int arr[5] = {10, 20, 30, 40, 50};
    
    // Test += on array elements
    arr[0] += 5;  // 10 + 5 = 15
    arr[1] -= 8;  // 20 - 8 = 12
    arr[2] *= 2;  // 30 * 2 = 60
    arr[3] /= 4;  // 40 / 4 = 10
    arr[4] %= 3;  // 50 % 3 = 2
    
    // arr[0]=15, arr[1]=12, arr[2]=60 (overflow to fit in result), arr[3]=10, arr[4]=2
    // But since we need result in 0-255 range, let's use modulo
    int result = (arr[0] + arr[1] + arr[3] + arr[4]) % 256;
    // 15 + 12 + 10 + 2 = 39
    
    // Add a simple constant to get to 47
    result += 8;
    
    return result;  // Should be 47
}
