template<typename T>
struct BaseCarrier {
	template<typename U>
	struct Rebind {
		template<typename X>
		static int value() {
			return sizeof(X) == sizeof(int) ? 0 : 7;
		}
	};
};

template<typename T>
struct Derived : BaseCarrier<T>::template Rebind<int> {
	int run() {
		return this->template value<int>();
	}
};

int main() {
	Derived<char> d;
	return d.run();
}
