// Final comprehensive test for comma-separated variables and if-statements

int main() {
    // Test 1: Simple comma-separated declarations
    int a = 5, b = 3;
    
    // Test 2: Comma-separated with if-statement
    int x = 10, y = 20, z = 30;
    if (x < y) {
        return x + y + z;
    }
    
    // Test 3: Nested if with comma-separated variables
    int p = 1, q = 2, r = 3;
    if (p > 0) {
        if (q > p) {
            if (r > q) {
                return p + q + r;
            }
        }
    }
    
    return 0;
}

