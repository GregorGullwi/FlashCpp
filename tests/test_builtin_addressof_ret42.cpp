// Test for __builtin_addressof intrinsic
// __builtin_addressof returns the actual address of an object, even if it has an overloaded operator&

struct HasOverloadedAddressOf {
	int value;
	
	HasOverloadedAddressOf(int v) : value(v) {}
	
	// Overloaded operator& that returns nullptr
	// This is intentionally bad behavior for testing purposes
	// to demonstrate that __builtin_addressof bypasses this overload
	HasOverloadedAddressOf* operator&() {
		return nullptr;
	}
};

int main() {
	HasOverloadedAddressOf obj(42);
	
	// Regular & operator returns nullptr (overloaded)
	HasOverloadedAddressOf* ptr1 = &obj;
	
	// __builtin_addressof should return the actual address
	HasOverloadedAddressOf* ptr2 = __builtin_addressof(obj);
	
	// ptr1 should be nullptr (overloaded operator&)
	// ptr2 should be the actual address and we can access the value
	if (ptr2 != nullptr) {
		return ptr2->value;
	}
	
	return 0;
}
