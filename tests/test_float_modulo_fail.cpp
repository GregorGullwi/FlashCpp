// C++20 [expr.mul]/4: % requires integral operands.
// float % float is ill-formed and should be rejected.
int main() {
    float a = 1.5f;
    float b = 2.0f;
    float r = a % b;  // ill-formed
    return 0;
}
