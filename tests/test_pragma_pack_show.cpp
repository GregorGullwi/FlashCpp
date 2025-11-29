// Test #pragma pack(show) to verify alignment tracking

#pragma pack(show)              // Should show: default

#pragma pack(4)
#pragma pack(show)              // Should show: 4

#pragma pack(push, 8)
#pragma pack(show)              // Should show: 8

#pragma pack(push, label1, 2)
#pragma pack(show)              // Should show: 2

#pragma pack(pop)
#pragma pack(show)              // Should show: 8

#pragma pack(pop)
#pragma pack(show)              // Should show: 4

#pragma pack()
#pragma pack(show)              // Should show: default

int main() {
    return 0;
}
