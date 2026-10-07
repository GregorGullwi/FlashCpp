template<typename T>
int readLocal(T value) {
	struct Local {
		T stored;
	};

	Local local{value};
	return local.stored == value ? 0 : 1;
}

template<int N>
int recursiveLocal() {
	struct Local {
		int stored;
	};

	Local local{N};
	if constexpr (N == 0) {
		return local.stored;
	} else {
		return local.stored + recursiveLocal<N - 1>();
	}
}

int main() {
	return readLocal<int>(7) + readLocal<double>(3.5) + (recursiveLocal<20>() == 210 ? 0 : 1);
}
