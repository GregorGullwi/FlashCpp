// Deleted copy constructor should be diagnosed for brace initialization.

struct NoCopy {
	NoCopy() = default;
	NoCopy(const NoCopy&) = delete;
};

int main() {
	NoCopy source;
	NoCopy copy{source};
	return 0;
}
