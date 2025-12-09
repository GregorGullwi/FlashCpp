// Test case for 9 double parameters (8th parameter is in XMM7, 9th goes on stack)
// This test should crash before the fix and pass after

double add_nine_doubles(double a, double b, double c, double d, double e, double f, double g, double h, double i) {
    return a + b + c + d + e + f + g + h + i;
}

int main() {
    double result = add_nine_doubles(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0);
    // Expected: 1+2+3+4+5+6+7+8+9 = 45.0
    return static_cast<int>(result);  // Should return 45
}
