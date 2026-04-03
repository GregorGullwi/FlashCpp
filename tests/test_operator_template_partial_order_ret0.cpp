namespace ns {
	template<typename T>
	struct Box {
		T value;
	};

	template<typename T>
	bool operator==(const Box<T>& a, const Box<T>& b) {
		return a.value == b.value;
	}
}

template<typename T>
bool operator==(const T& a, const T& b) {
	return false;
}

int main() {
	ns::Box<int> a{42};
	ns::Box<int> b{42};
	return (a == b) ? 0 : 1;
}
