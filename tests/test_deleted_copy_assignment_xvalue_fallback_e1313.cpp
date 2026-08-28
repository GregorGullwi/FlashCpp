// Deleted copy assignment should still be diagnosed for xvalue assignment
// when no move assignment operator exists.

// Diagnostic regression for deleted copy assignment.
struct NoCopyAssignFallback {
	NoCopyAssignFallback() = default;
	~NoCopyAssignFallback() {}
	NoCopyAssignFallback& operator=(const NoCopyAssignFallback&) = delete;
};

int main() {
	NoCopyAssignFallback source;
	NoCopyAssignFallback dest;
	dest = static_cast<NoCopyAssignFallback&&>(source);
	return 0;
}
