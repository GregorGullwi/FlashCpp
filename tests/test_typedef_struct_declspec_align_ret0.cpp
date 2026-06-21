// Regression: MSVC headers use typedef'd struct definitions with __declspec
// between the struct keyword and the tag name.

typedef struct __declspec(align(16)) _AlignedTag
{
	unsigned __int64 parts[2];
} AlignedTag;

int main() {
	AlignedTag value{};
	value.parts[0] = 7;
	(void)value;
	return 0;
}
