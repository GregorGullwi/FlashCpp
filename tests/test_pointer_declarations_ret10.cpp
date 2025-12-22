int main() {
    // Test various pointer declaration syntaxes
    int x = 10;
    
    // Basic pointer
    int* p1 = &x;
    
    // Pointer with const after *
    int* const p2 = &x;
    
    // Pointer to const
    const int* p3 = &x;
    
    // Multi-level
    int** pp1 = &p1;
    
    // Multi-level with const
    int* const* pp2 = &p2;
    const int** pp3 = &p3;
    
    // Triple pointer
    int*** ppp = &pp1;
    
    return ***ppp;
}

