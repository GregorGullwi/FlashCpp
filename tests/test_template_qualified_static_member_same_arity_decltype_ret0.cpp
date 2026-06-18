template <typename T>
struct Holder {
	static char pick(int) {
		return 1;
	}

	static long pick(long) {
		return 2;
	}
};

template <typename T>
using PickResult = decltype(Holder<T>::pick(0L));

int main() {
	return sizeof(PickResult<int>) == sizeof(long) ? 0 : 1;
}
