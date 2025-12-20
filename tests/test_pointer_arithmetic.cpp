// Test pointer arithmetic with different pointer levels
int main() {
    int values[3] = {10, 20, 30};
    
    // Create an array of int* pointers
    int* ptrs[3];
    ptrs[0] = &values[0];
    ptrs[1] = &values[1];
    ptrs[2] = &values[2];
    
    // Point to the first pointer in the array
    int** pp = &ptrs[0];
    
    // CRITICAL TEST: pp + 1 should advance by sizeof(int*) = 8 bytes
    // This now tests pointer arithmetic on a proper array
    int** pp2 = pp + 1;
    
    // pp2 should point to ptrs[1], so **pp2 should be 20
    int result = **pp2;
    
    return result;  // Should return 20 if pointer arithmetic is correct
}
