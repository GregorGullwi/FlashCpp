struct Source {
	int value;
};

struct Target {
	int value;

	explicit Target(const Target& other) : value(other.value) {}
	Target(const Source& src) : value(src.value) {}
};

// Second test: primitive arg where explicit ctor exists alongside non-explicit
struct Target2 {
	int value;

	explicit Target2(int x) : value(x) {}
	Target2(double d) : value(static_cast<int>(d)) {}
};

// Third test: template-instantiated argument type (Type::UserDefined)
// This exercises the type_index matching code at IrGenerator_Call_Direct.cpp:754-760
// because template instantiations produce Type::UserDefined != Type::Struct,
// which enters the converting constructor check block at line 729.
template <typename T>
struct Wrapper {
	T value;
};

struct Receiver {
	int value;

	explicit Receiver(const Receiver& other) : value(other.value) {}
	Receiver(const Wrapper<int>& w) : value(w.value) {}
};

int useTarget(Target t) {
	return t.value;
}

int useTarget2(Target2 t) {
	return t.value;
}

int useReceiver(Receiver r) {
	return r.value;
}

int main() {
	Source s{42};
	int r1 = useTarget(s);
	int r2 = useTarget2(42.0);
	Wrapper<int> w{42};
	int r3 = useReceiver(w);
	return (r1 == 42 && r2 == 42 && r3 == 42) ? 42 : 1;
}
