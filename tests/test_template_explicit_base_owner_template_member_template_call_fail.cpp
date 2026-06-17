// TODO(template-infra): currently fails while parsing `this->Base<T>::template ...`
// because the postfix qualified-owner path drops owner template-ids before the
// `::template` continuation is analyzed.
template <class T>
struct Base {
	template <class U>
	int pick(U value = U{1}) {
		return static_cast<int>(value);
	}
};

template <class T>
struct Holder : Base<T> {
	template <class U>
	int pick(U value) {
		return static_cast<int>(value) + 100;
	}

	int run() {
		return this->Base<T>::template pick<int>() - 1;
	}
};

int main() {
	return Holder<long>{}.run();
}
