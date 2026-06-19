struct Base {
	template <class U>
	long operator()(U value = U{1}) {
		return static_cast<long>(value);
	}
};

template <class T>
struct Holder : Base {
	template <class U>
	char operator()(U value) {
		return static_cast<char>(value);
	}
};

template <class T>
auto callBase(Holder<T>& holder)
	-> decltype(holder.Base::template operator()<int>()) {
	return holder.Base::template operator()<int>();
}

int main() {
	Holder<void> holder;
	return callBase(holder) == 1 ? 0 : 1;
}
