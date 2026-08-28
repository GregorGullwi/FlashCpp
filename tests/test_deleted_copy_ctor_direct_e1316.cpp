// Deleted copy constructor should be diagnosed for direct initialization.

// Diagnostic regression for deleted copy constructor.
struct NoCopy {
	NoCopy() = default;
	NoCopy(const NoCopy&) = delete;
};

int main() {
	NoCopy source;
	NoCopy copy(source);
	return 0;
}
