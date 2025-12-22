// Test: Many local variables to trigger register spilling
// This test verifies that the compiler can handle more variables than available registers

int main() {
    int a = 1;
    int b = 2;
    int c = 3;
    int d = 4;
    int e = 5;
    int f = 6;
    int g = 7;
    int h = 8;
    int i = 9;
    int j = 10;
    int k = 11;
    int l = 12;
    int m = 13;
    int n = 14;
    int o = 15;
    int p = 16;
    int q = 17;
    int r = 18;
    int s = 19;
    int t = 20;
    
    // Use all variables to prevent optimization
    int result = a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t;
    
    return result;  // Should return 210
}

