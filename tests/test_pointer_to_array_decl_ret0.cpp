// Regression test: pointer-to-array declarator parsing
// Handles _Tp (*__p)[_Nm] pattern used in standard library headers
// This tests the parser's ability to handle this declarator form.

void setFirst(int (*arr)[3], int val) {
    // Use pointer-to-array parameter
    (*arr)[0] = val;
}

int main() {
    int data[3] = {0, 0, 0};
    // Test that pointer-to-array declarations compile correctly
    int (*ptr)[3] = &data;
    (void)ptr;
    return 0;
}
