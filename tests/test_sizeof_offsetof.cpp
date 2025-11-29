// Test sizeof and offsetof with pragma pack

#pragma pack(1)
struct Packed1 {
    char c;      // offset 0, size 1
    int i;       // offset 1, size 4
    short s;     // offset 5, size 2
};               // total size: 7 bytes
#pragma pack()

#pragma pack(2)
struct Packed2 {
    char c;      // offset 0, size 1
    int i;       // offset 2 (aligned to 2), size 4
    short s;     // offset 6, size 2
};               // total size: 8 bytes
#pragma pack()

struct Default {
    char c;      // offset 0, size 1
    int i;       // offset 4 (aligned to 4), size 4
    short s;     // offset 8, size 2
};               // total size: 12 bytes (with padding to 4-byte alignment)

int main() {
    // Test sizeof with pack(1)
    unsigned long long size1 = sizeof(Packed1);
    if (size1 != 7) {
        return 1;  // Error: pack(1) struct should be 7 bytes
    }
    
    // Test offsetof with pack(1)
    unsigned long long offset1_c = offsetof(Packed1, c);
    unsigned long long offset1_i = offsetof(Packed1, i);
    unsigned long long offset1_s = offsetof(Packed1, s);
    
    if (offset1_c != 0) return 2;
    if (offset1_i != 1) return 3;  // No padding with pack(1)
    if (offset1_s != 5) return 4;
    
    // Test sizeof with pack(2)
    unsigned long long size2 = sizeof(Packed2);
    if (size2 != 8) {
        return 5;  // Error: pack(2) struct should be 8 bytes
    }
    
    // Test offsetof with pack(2)
    unsigned long long offset2_c = offsetof(Packed2, c);
    unsigned long long offset2_i = offsetof(Packed2, i);
    unsigned long long offset2_s = offsetof(Packed2, s);
    
    if (offset2_c != 0) return 6;
    if (offset2_i != 2) return 7;  // Aligned to 2 with pack(2)
    if (offset2_s != 6) return 8;
    
    // Test sizeof with default packing
    unsigned long long size_default = sizeof(Default);
    if (size_default != 12) {
        return 9;  // Error: default struct should be 12 bytes
    }
    
    // Test offsetof with default packing
    unsigned long long offset_default_c = offsetof(Default, c);
    unsigned long long offset_default_i = offsetof(Default, i);
    unsigned long long offset_default_s = offsetof(Default, s);
    
    if (offset_default_c != 0) return 10;
    if (offset_default_i != 4) return 11;  // Aligned to 4 by default
    if (offset_default_s != 8) return 12;
    
    return 0;  // Success!
}

