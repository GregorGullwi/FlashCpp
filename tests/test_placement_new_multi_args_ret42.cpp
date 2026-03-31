// Test: placement new with multiple placement arguments (new (arg1, arg2) Type)
// Before fix: only arg1 was stored; arg2 was silently dropped
// After fix: all args are stored in placement_args_

// Custom two-arg placement operator new
void* operator new(unsigned long, void* ptr, int* tag) {
	*tag = 1;  // mark that it was called
	return ptr;
}

int main() {
	alignas(int) char buf[sizeof(int)];
	int tag = 0;
	// Multi-arg placement new: new (buf, &tag) int(42)
	int* p = new (buf, &tag) int(42);
	// Both args should have been parsed (not dropped)
	return (*p == 42) ? 42 : 0;
}
