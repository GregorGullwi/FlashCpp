struct Inner {
	int value;
};

struct SmartPtr {
	Inner* raw;

	Inner* operator->() {
		return raw;
	}
};

template <typename T>
struct Holder {
	SmartPtr ptr;

	int run() {
		return ptr->value;
	}
};

int main() {
	Inner inner{42};
	Holder<int> h{{&inner}};
	return h.run();
}
