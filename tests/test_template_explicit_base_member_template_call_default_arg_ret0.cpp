struct Base {
	template <class U>
	int pick(U value = U{1}) {
		return static_cast<int>(value);
	}
};

template <class T>
struct Holder : Base {
	template <class U>
	int pick(U value) const {
		return static_cast<int>(value) + 100;
	}

	int run() {
		return this->Base::template pick<int>() - 1;
	}
};

int main() {
	return Holder<int>{}.run();
}
