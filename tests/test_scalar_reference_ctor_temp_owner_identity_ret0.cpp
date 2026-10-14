namespace First {
struct Receiver {
	long value;
	Receiver(const long& source) : value(source + 1) {}
};
struct Holder {
	Receiver receiver;
	Holder(short source) : receiver(source) {}
};
}

namespace Second {
struct Receiver {
	double value;
	Receiver(const double& source) : value(source + 2) {}
};
struct Holder {
	Receiver receiver;
	Holder(short source) : receiver(source) {}
};
}

template<class T> struct Receiver {
	T value;
	Receiver(const T& source) : value(source) {}
};
template<class T> struct Holder {
	Receiver<T> receiver;
	Holder(short source) : receiver(source) {}
};

int main() {
	First::Holder first(7);
	Second::Holder second(9);
	Holder<long> integer(11);
	Holder<double> floating(13);
	return first.receiver.value == 8 && second.receiver.value == 11 && integer.receiver.value == 11 && floating.receiver.value == 13 ? 0 : 1;
}
