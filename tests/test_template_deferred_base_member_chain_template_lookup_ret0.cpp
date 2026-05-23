template<typename T>
struct BaseCarrier {
	struct Mid {
		template<typename X>
		static int value() {
			return sizeof(X) == sizeof(int) ? 0 : 7;
		}
	};
};

template<typename T>
struct Derived : BaseCarrier<T>::Mid {
	int run() {
		return this->template value<int>();
	}
};

int main() {
	Derived<char> d;
	return d.run();
}
