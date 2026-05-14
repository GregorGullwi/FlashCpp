namespace Alpha {
	template <typename T>
	int id(T) {
		return 42;
	}
}

namespace Beta {
	template <typename T>
	int id(T) {
		return 7;
	}
}

int main() {
	return Alpha::id<int>(0);
}
