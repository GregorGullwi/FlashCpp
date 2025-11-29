// Test: Many local variables to trigger register spilling
// This test verifies that the compiler can handle more variables than available registers

// Test: Register spilling with many floating-point variables
// x86-64 has 16 XMM registers (XMM0-XMM15)
// This test uses more than 16 float variables to trigger XMM register spilling

float get_floats() {
    float a = 1.0f;
    float b = 2.0f;
    float c = 3.0f;
    float d = 4.0f;
    float e = 5.0f;
    float f = 6.0f;
    float g = 7.0f;
    float h = 8.0f;
    float i = 9.0f;
    float j = 10.0f;
    float k = 11.0f;
    float l = 12.0f;
    float m = 13.0f;
    float n = 14.0f;
    float o = 15.0f;
    float p = 16.0f;
    float q = 17.0f;
    float r = 18.0f;
    
    // Use all variables in a computation
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r;
}

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
    int result = a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t + static_cast<int>(get_floats());
    
    return result;  // Should return 210 + 171 =381 
}

