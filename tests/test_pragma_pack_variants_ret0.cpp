// Test various #pragma pack syntaxes

#pragma pack(push)           // Simple push
#pragma pack(push, 8)        // Push with alignment
#pragma pack(pop)            // Simple pop

#pragma pack(push, mylabel)  // Push with identifier
#pragma pack(push, mylabel, 4) // Push with identifier and alignment
#pragma pack(pop, mylabel)   // Pop with identifier

#pragma pack(8)              // Set alignment directly
#pragma pack()               // Reset to default

int main() {
    return 0;
}
