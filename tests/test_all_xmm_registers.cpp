// Test: Verify all 16 XMM registers are available
// This test uses exactly 16 float variables to use all XMM0-XMM15 registers
// without triggering spilling (if the compiler allocates them properly)

int main() {
    // Declare 16 float variables to potentially use all 16 XMM registers
    float v0 = 1.0f;
    float v1 = 2.0f;
    float v2 = 3.0f;
    float v3 = 4.0f;
    float v4 = 5.0f;
    float v5 = 6.0f;
    float v6 = 7.0f;
    float v7 = 8.0f;
    float v8 = 9.0f;
    float v9 = 10.0f;
    float v10 = 11.0f;
    float v11 = 12.0f;
    float v12 = 13.0f;
    float v13 = 14.0f;
    float v14 = 15.0f;
    float v15 = 16.0f;
    
    // Use all variables in a computation
    float result = v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + 
                   v8 + v9 + v10 + v11 + v12 + v13 + v14 + v15;
    
    // Convert to int for return (136.0 -> 136)
    int iresult = result;
    return iresult;
}

