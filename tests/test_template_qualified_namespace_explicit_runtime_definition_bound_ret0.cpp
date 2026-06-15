namespace QualifiedRuntimeNs {
	template <typename U>
	int pick(U) {
		return 3;
	}
}

template <typename T>
struct Holder {
	static int run() {
		return QualifiedRuntimeNs::pick<T>(0) == 3 ? 0 : 1;
	}
};

namespace QualifiedRuntimeNs {
	template <typename U>
	int pick(int) {
		return 5;
	}
}

int main() {
	return Holder<int>::run();
}
