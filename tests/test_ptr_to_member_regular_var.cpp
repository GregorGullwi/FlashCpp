// Test .* with regular int variable (not pointer-to-member type)
struct Point {
    int x;
};

int main() {
    Point p = {10};
    int regular_var = 5;
    
    // Try .* with a regular int (should fail semantically but test parsing)
    p.*regular_var;
    
    return 0;
}
