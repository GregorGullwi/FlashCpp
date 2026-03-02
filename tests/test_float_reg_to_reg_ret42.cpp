// Test float register-to-register moves
// This exercises the SSE MOVSS/MOVSD reg-to-reg path in the IR converter
double compute(double a, double b) {
    double temp = a + b;
    double result = temp * 2.0;
    return result;
}

int main() {
    double val = compute(10.5, 10.5);
    // val should be 42.0
    return static_cast<int>(val);
}
