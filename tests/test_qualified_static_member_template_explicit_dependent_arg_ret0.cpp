template <class T>
struct Source {
	static T make() {
		return T{};
	}
};

struct Host {
	template <class U>
	static int pick(U) {
		return 0;
	}

	template <class U>
	static int pick(U*) {
		return 1;
	}
};

template <class T>
int run() {
	return Host::template pick<T*>(Source<T*>::make());
}

int main() {
	return run<int>();
}
