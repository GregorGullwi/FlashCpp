struct Base {
	int operator()(int value = 1) const {
		return value;
	}
};

template <class T>
struct Holder : Base {
	int operator()(long value) const {
		return static_cast<int>(value) + 100;
	}

	int run() const {
		return this->Base::operator()() - 1;
	}
};

int main() {
	return Holder<int>{}.run();
}
