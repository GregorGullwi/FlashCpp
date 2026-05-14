namespace ns {
	template<typename T>
	int select(T*) {
		return 7;
	}

	template<typename T>
	int select(T) {
		return 42;
	}
}

template<typename U>
int call_explicit(U value) {
	return ns::select<int>(value);
}

int main() {
	return call_explicit<int>(1);
}
