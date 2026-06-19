struct Base {
	template <class U>
	char operator()(U value = U{1}) {
		return static_cast<char>(value + 10);
	}
};

template <class T>
struct Outer {
	struct Base {
		template <class U>
		long operator()(U value = U{2}) {
			return static_cast<long>(value + 40);
		}
	};

	struct Holder : Base {};

	static auto call(Holder& holder)
		-> decltype(holder.Base::template operator()<int>()) {
		return holder.Base::template operator()<int>();
	}
};

int main() {
	Outer<void>::Holder holder;
	return sizeof(decltype(Outer<void>::call(holder))) == sizeof(long) &&
			Outer<void>::call(holder) == 42
		? 0
		: 1;
}
