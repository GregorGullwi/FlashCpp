int pick(long) { return 40; }

template<class T>
struct Base {
	template<class U>
	int call(U) { return 2; }
};

template<class T>
struct Derived : Base<T> {
	template<class U>
	int run(U);
};

template<class T>
template<class U>
int Derived<T>::run(U u) {
	return pick(0) + this->template call<U>(u);
}

int pick(int) { return 100; }

int main() {
	Derived<int> d;
	return d.run(0) == 42 ? 0 : 1;
}
