struct S {
    int v;
};

int main() {
    S s1, s2, s3, s4, s5;
    s1.v = 1;
    s2.v = 2;
    s3.v = 3;
    s4.v = 4;
    s5.v = 5;
    
    // This should work (3 operands)
    int sum3 = s1.v + s2.v + s3.v;
    
    // This crashes due to codegen bug (5 operands)
    int sum5 = s1.v + s2.v + s3.v + s4.v + s5.v;
    
    return sum5 - 15;  // Should return 0 if fixed
}