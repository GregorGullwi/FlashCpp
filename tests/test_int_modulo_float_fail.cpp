// C++20 [expr.mul]/4: %= requires integral operands.
// int %= double is ill-formed (common type becomes double).
int main() {
    int i = 10;
    double d = 3.5;
    i %= d;  // ill-formed
    return 0;
}
