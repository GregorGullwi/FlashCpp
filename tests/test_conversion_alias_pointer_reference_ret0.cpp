// Test: alias resolution through pointer and reference layers.
// Verifies overload resolution sees aliases as their underlying primitive types.

using IntAlias = int;
using IntAliasChain = IntAlias;
using IntAliasPtr = IntAliasChain*;
using IntAliasRef = IntAliasChain&;
using ConstIntAliasPtr = const IntAliasChain*;

int pickPointer(int* value) {
	return *value;
}

int pickPointer(long* value) {
	return (int)(*value + 100);
}

int pickConstPointer(const int* value) {
	return *value;
}

int pickConstPointer(const long* value) {
	return (int)(*value + 100);
}

int pickReference(const int& value) {
	return value;
}

int pickReference(const long& value) {
	return (int)(value + 100);
}

int main() {
	IntAliasChain value = 42;
	IntAliasPtr ptr = &value;
	IntAliasRef ref = value;
	ConstIntAliasPtr const_ptr = ptr;

	if (pickPointer(ptr) != 42) return 1;
	if (pickReference(ref) != 42) return 2;
	if (pickConstPointer(const_ptr) != 42) return 3;

	return 0;
}
