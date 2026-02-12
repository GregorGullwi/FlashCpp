// Test multiple references

int main() {
    int arr[5] = {10, 20, 30, 40, 50};
    
    int& ref0 = arr[0];
    ref0 += 5;  // 10 + 5 = 15
    
    int& ref1 = arr[1];
    ref1 -= 8;  // 20 - 8 = 12
    
    int& ref2 = arr[2];
    ref2 *= 2;  // 30 * 2 = 60
    
    int& ref3 = arr[3];
    ref3 /= 4;  // 40 / 4 = 10
    
    int& ref4 = arr[4];
    ref4 %= 3;  // 50 % 3 = 2
    
    return arr[0] + arr[1] + arr[3] + arr[4];  // 15 + 12 + 10 + 2 = 39
}
