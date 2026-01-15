// Test typedef array syntax support
// This is a common C/C++ pattern used in system headers like <csetjmp>

// Simple 1D array typedef (like in bits/setjmp.h)
typedef long int JmpBufArray[8];

// Typedef with smaller type
typedef char CharBuffer[64];

int main() {
    JmpBufArray jb;
    jb[0] = 42;
    jb[1] = 100;
    
    CharBuffer buf;
    buf[0] = 'H';
    buf[1] = 'i';
    buf[2] = 0;
    
    // Verify values
    if (jb[0] != 42) return 1;
    if (jb[1] != 100) return 2;
    if (buf[0] != 'H') return 3;
    
    return 0;
}
