template<unsigned long Size, unsigned long Align>
struct AlignedLike {
	using type = int;
};

struct Holder {
	void* ptr;
	AlignedLike<sizeof(ptr), alignof(void*)>::type value;
};

int main() {
	Holder h{};
	(void)h;
	return 0;
}
