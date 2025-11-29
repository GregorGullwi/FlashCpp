// Test that union size is the max of its members
union U {
    char c;      // 1 byte
    int i;       // 4 bytes  
    double d;    // 8 bytes
};

int main() {
    U u;
    u.d = 3.14;
    u.i = 100;
    u.c = 'Z';
    
    // Union should be 8 bytes (size of largest member)
    int size = sizeof(U);
    return size;  // Should return 8
}
