struct Inner {
	int value;
};

template <typename T>
struct Holder {
	Inner* forward(Inner* ptr) {
		return ptr;
	}

	int run() {
		Inner inner{42};
		return forward(&inner)->value;
	}
};

int main() {
	Holder<int> h;
	return h.run();
}
