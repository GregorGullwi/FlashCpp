// Test: alignas with expression arguments
// Validates parsing of alignas(__alignof__(...)) and alignas(sizeof(...))
struct alignas(16) AlignedStruct {
    int x;
};

struct Wrapper {
    alignas(alignof(double)) unsigned char storage[sizeof(double)];
};

int main() {
	AlignedStruct as;
	return alignof(AlignedStruct);  // 16
}