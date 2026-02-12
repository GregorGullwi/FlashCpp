// Test array subscript compound assignment WITHOUT references

int main() {
    int arr[3] = {10, 20, 30};
    
    // Direct array subscript compound assignment (no references)
    arr[0] += 5;   // 10 + 5 = 15
    arr[1] -= 5;   // 20 - 5 = 15  
    arr[2] *= 2;   // 30 * 2 = 60
    
    // Result: 15 + 15 + 60 = 90
    return arr[0] + arr[1] + arr[2];
}
