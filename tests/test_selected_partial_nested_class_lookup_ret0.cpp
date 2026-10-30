struct Base {
	int base_tag;
};

template<class T> struct Outer {
	T value;
	struct Key : Base {
		using type = T;
		T* pointer;
		int tag;
	};
};

template<class T> struct Outer<T*> {
	T* value;
	struct Key : Base {
		using type = T;
		T* pointer;
		int tag;
	};
};

using ShortKey = Outer<short*>::Key;
using LongKey = Outer<long long*>::Key;

static_assert(sizeof(ShortKey::type) == sizeof(short));
static_assert(sizeof(LongKey::type) == sizeof(long long));
static_assert(sizeof(ShortKey) == 3 * sizeof(void*));
static_assert(sizeof(LongKey) == 3 * sizeof(void*));

using Short = ShortKey::type;

int main() {
	ShortKey short_key{};
	LongKey long_key{};
	short_key.pointer = nullptr;
	long_key.pointer = nullptr;
	short_key.tag = 3;
	long_key.tag = 5;
	short_key.base_tag = 2;
	long_key.base_tag = 4;
	Short total = short_key.tag + long_key.tag + short_key.base_tag + long_key.base_tag;
	return total - 14;
}
