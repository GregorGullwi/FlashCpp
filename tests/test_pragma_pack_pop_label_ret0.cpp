// Test #pragma pack(pop, identifier) - should restore to the state when that identifier was pushed

#pragma pack(show)              // Should show: default

#pragma pack(push, label1, 2)
#pragma pack(show)              // Should show: 2

#pragma pack(push, label2, 4)
#pragma pack(show)              // Should show: 4

#pragma pack(push, label3, 8)
#pragma pack(show)              // Should show: 8

// Now pop directly to label1 (skipping label2 and label3)
#pragma pack(pop, label1)
#pragma pack(show)              // Should show: 2 if named labels work, or default if using simple stack

int main() {
    return 0;
}
