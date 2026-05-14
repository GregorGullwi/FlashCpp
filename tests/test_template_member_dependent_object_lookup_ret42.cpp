template <class T>
struct Holder {
	template <class U>
	int value(U v) {
		return 40 + v;
	}
};

template <class T>
int call_member_template() {
	Holder<T> holder;
	return holder.template value<int>(2);
}

int main() {
	return call_member_template<void>();
}
