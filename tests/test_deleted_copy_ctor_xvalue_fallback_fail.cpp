// Deleted copy constructor should still be diagnosed for xvalue copy-init
// when no move constructor exists.

struct NoCopyCtorFallback {
	NoCopyCtorFallback() = default;
	~NoCopyCtorFallback() {}
	NoCopyCtorFallback(const NoCopyCtorFallback&) = delete;
};

int main() {
	NoCopyCtorFallback source;
	NoCopyCtorFallback moved = static_cast<NoCopyCtorFallback&&>(source);
	return 0;
}
