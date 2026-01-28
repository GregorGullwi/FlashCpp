// Test #pragma pack with identifier - push and pop to specific labels

#pragma pack(push, label1, 4)  // Push with label1, set to 4
// Alignment should be 4 here

#pragma pack(push, label2, 8)  // Push with label2, set to 8
// Alignment should be 8 here

#pragma pack(pop, label2)      // Pop back to label2 state (should restore to state before label2)
// Should be back to 4

#pragma pack(pop, label1)      // Pop back to label1 state
// Should be back to original

int main() {
    return 0;
}
