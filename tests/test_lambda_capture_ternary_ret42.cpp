// Test lambda [=] capture of variables used in ternary expressions
int compute(int val) {
    const bool neg = val < 0;
    const int adj = 2;
    auto fn = [=](int x) { return neg ? x + adj : x; };
    return fn(40);  // neg=true since val=-1, returns 40+2 = 42
}

int main() {
    return compute(-1);
}
