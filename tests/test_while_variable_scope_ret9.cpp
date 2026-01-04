// Test sequential while loops with same variable names in bodies
// This verifies that variable scopes are properly managed in while loop bodies
int main() {
    int sum = 0;
    int count = 0;
    
    // First while loop with variable x
    while (count < 3) {
        int x = count;
        sum += x;
        count++;
    }
    // Adds: 0 + 1 + 2 = 3
    
    // Reset count for second loop
    count = 0;
    
    // Second while loop - reusing variable name x should work
    while (count < 3) {
        int x = count * 2;
        sum += x;
        count++;
    }
    // Adds: 0 + 2 + 4 = 6
    
    return sum;  // Expected: 3 + 6 = 9
}
