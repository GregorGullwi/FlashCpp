struct Host {
	template <class U>
	static int value() {
		return 42;
	}
};

template <class T>
int call_member_template() {
	return Host::template value<T>();
}

template <class T>
struct Host2 {
	template <class U>
	static int value(U v) {
		return v;
	}

	static int run() {
		return Host2::template value<int>(0);
	}
};

int main() {
	return call_member_template<int>() + Host2<void>::run();
}
