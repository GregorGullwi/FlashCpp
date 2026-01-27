// Test: Register spilling with many floating-point variables
// x86-64 has 16 XMM registers (XMM0-XMM15)
// This test uses 20 float variables to trigger XMM register spilling

int main() {
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
    float s = 19.0f;
    float t = 20.0f;

    // Use all variables in a computation
    float result = a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t;

    // Convert to int for return (210.0 -> 210)
    int iresult = result;
    return iresult;
}

