namespace lib {
	struct Value {
		int n;
	};
}

template <class T>
int call_dependent(T value) {
	return f(value);
}

namespace lib {
	int f(Value value) {
		return value.n;
	}
}

int main() {
	lib::Value value{42};
	return call_dependent(value) - 42;
}
