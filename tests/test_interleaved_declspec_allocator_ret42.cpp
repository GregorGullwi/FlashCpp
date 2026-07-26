// Microsoft extends decl-specifier-seq with __declspec. The MSVC STL places
// __declspec(allocator) between constexpr and the return type.

static __declspec(noinline) int freeFunction() {
	return 1;
}

struct AllocatorLike {
	__declspec(noinline) static constexpr int leadingAttribute() {
		return 2;
	}

	static __declspec(noinline) constexpr int middleAttribute() {
		return 4;
	}

	static constexpr __declspec(allocator) int allocate() {
		return 35;
	}
};

int main() {
	return freeFunction() +
		AllocatorLike::leadingAttribute() +
		AllocatorLike::middleAttribute() +
		AllocatorLike::allocate();
}
