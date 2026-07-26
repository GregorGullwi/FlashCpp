// Microsoft extends decl-specifier-seq with __declspec. CV qualifiers remain
// defining-type-specifiers and may appear on either side of that extension.

static const __declspec(align(16)) int const_value = 20;
static volatile __declspec(align(16)) int volatile_value = 22;

int main() {
	return const_value + volatile_value;
}
