template<typename T>
int select_overload(const T* ptr) {
	(void)ptr;
	return T::nonexistent_member;
}

template<typename T>
int select_overload(T* ptr) {
	(void)ptr;
	return 42;
}

int main() {
	int value = 0;
	return select_overload<int>(&value);
}
