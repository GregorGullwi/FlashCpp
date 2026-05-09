struct NonSwappable {
	NonSwappable() = default;
	NonSwappable(const NonSwappable&) = default;
};

void swap(NonSwappable&, NonSwappable&) = delete;

template<typename T>
struct Holder {
	T value;

	void swapWith(Holder& other) {
		swap(value, other.value);
	}
};

int main() {
	Holder<NonSwappable> holder;
	(void)holder;
	return 0;
}
