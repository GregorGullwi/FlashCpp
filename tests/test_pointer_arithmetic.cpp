// Test pointer arithmetic with different pointer levels
int main() {
    int a = 10;
    int b = 20;
    int c = 30;
    
    // Create a "fake array" of int* by putting them consecutively on stack
    // This relies on compiler allocating them sequentially
    int* p1 = &a;  // p1 = 10
    int* p2 = &b;  // p2 = 20  
    int* p3 = &c;  // p3 = 30
    
    // Point to p1
    int** pp = &p1;
    
    // CRITICAL TEST: pp + 1 should advance by sizeof(int*) = 8 bytes
    // Before fix: would advance by sizeof(int) = 4 bytes (WRONG)
    // After fix: should advance by 8 bytes (CORRECT)
    int** pp2 = pp + 1;
    
    // If the fix works, pp2 points to p2, so **pp2 = 20
    // If broken, pp2 points to garbage (middle of p1's 8 bytes)
    int result = **pp2;
    
    return result;  // Should return 20 if pointer arithmetic is fixed
}
