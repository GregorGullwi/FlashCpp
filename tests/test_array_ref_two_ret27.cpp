// Test two references

int main() {
    int arr[3] = {10, 20, 30};
    
    int& ref0 = arr[0];
    ref0 += 5;  // 10 + 5 = 15
    
    int& ref1 = arr[1];
    ref1 -= 8;  // 20 - 8 = 12
    
    return arr[0] + arr[1];  // 15 + 12 = 27
}
