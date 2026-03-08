// Test: float-to-double conversion under XMM register pressure.
// With 15 live float values occupying XMM registers, the allocator
// may assign the same XMM to both source and result in handleFloatToFloat.
// Without the guard (source_xmm != result_xmm), releasing source_xmm
// would free the result register before storing it.

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

    // (double)a triggers handleFloatToFloat while b..o occupy XMM registers
    double sum = (double)a + b + c + d + e + f + g + h + i + j + k + l + m + n + o;

    return (int)sum; // 1+2+...+15 = 120
}
