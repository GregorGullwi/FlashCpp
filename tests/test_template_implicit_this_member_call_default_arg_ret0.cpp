// Regression: implicit this-member calls inside template bodies must preserve
// the concrete current-member call path, including default arguments.

template <class T>
struct Holder {
	int touch(int value = 1) const {
		return value;
	}

	int run() const {
		return touch() - 1;
	}
};

int main() {
	return Holder<int>{}.run();
}
