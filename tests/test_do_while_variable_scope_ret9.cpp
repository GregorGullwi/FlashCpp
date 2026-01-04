// Test sequential do-while loops with same variable names in bodies
// This verifies that variable scopes are properly managed in do-while loop bodies
int main() {
    int sum = 0;
    int count = 0;
    
    // First do-while loop with variable x
    do {
        int x = count;
        sum += x;
        count++;
    } while (count < 3);
    // Adds: 0 + 1 + 2 = 3
    
    // Reset count for second loop
    count = 0;
    
    // Second do-while loop - reusing variable name x should work
    do {
        int x = count * 2;
        sum += x;
        count++;
    } while (count < 3);
    // Adds: 0 + 2 + 4 = 6
    
    return sum;  // Expected: 3 + 6 = 9
}
