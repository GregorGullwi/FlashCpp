int main() {
    int a = 5;
    int b = ~a;    // BitwiseNot
    int c = -b;    // Negate
    int d = !c;    // LogicalNot
    int e = ++a;   // PreIncrement
    int f = d--;   // PostDecrement
    return e + f;
}
