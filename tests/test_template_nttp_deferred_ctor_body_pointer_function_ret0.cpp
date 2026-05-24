int gValue = 11;

int getOffset() {
	return 7;
}

template<auto Ptr, auto Fn>
struct DeferredCtorNttp {
	static constexpr int marker = 1;

	int value;

	DeferredCtorNttp() : value(0) {
		value = *Ptr + Fn();
	}
};

int main() {
	DeferredCtorNttp<&gValue, &getOffset> obj;

	if (obj.value != 18) return 1;
	return 0;
}
