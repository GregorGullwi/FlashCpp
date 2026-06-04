// Test operator& overload - baseline without overload resolution
// This test documents the CURRENT behavior before implementing operator overload resolution

struct AddressProvider {
	int value;

	// Overloaded operator& - should return custom value
	// Currently NOT called by & operator (both & and __builtin_addressof behave the same)
	int* operator&() {
		static int custom_value = 99;
		return &custom_value;
	}
};

int main() {
	AddressProvider obj;
	obj.value = 42;

	// Unary & on class type calls overloaded operator&.
	int* actual_addr = &obj;

	// __builtin_addressof should always get actual address (bypass overload)
	AddressProvider* builtin_addr = __builtin_addressof(obj);

	if (*actual_addr != 99) {
		return 1;
	}
	if (builtin_addr->value != 42) {
		return 2;
	}
	return *actual_addr;
}
