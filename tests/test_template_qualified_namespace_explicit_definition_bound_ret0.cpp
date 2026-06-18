namespace QualifiedNs {
	template <typename U>
	char pick(U) {
		return 0;
	}
}

template <typename T>
struct Holder {
	using PickType = decltype(QualifiedNs::pick<T>(0));

	static int run() {
		return sizeof(PickType) == sizeof(char) ? 0 : 1;
	}
};

namespace QualifiedNs {
	template <typename U>
	long pick(int) {
		return 1;
	}
}

int main() {
	return Holder<int>::run();
}
