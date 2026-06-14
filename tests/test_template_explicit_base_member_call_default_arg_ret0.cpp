struct Base {
	int touch(int value = 1) const {
		return value;
	}
};

template <class T>
struct Holder : Base {
	int touch(long value) const {
		return static_cast<int>(value) + 100;
	}

	int run() const {
		return this->Base::touch() - 1;
	}
};

int main() {
	return Holder<int>{}.run();
}
