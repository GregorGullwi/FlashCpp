template <unsigned long long N, unsigned long long Alignment>
struct AlignedStorage {
	alignas(Alignment) unsigned char data[N];
};

struct MutexLike {
	union {
		int tag{};
		AlignedStorage<64, alignof(void*)> storage;
	};
};

int main() {
	MutexLike value{};
	return value.tag;
}
