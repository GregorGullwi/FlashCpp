struct Host {
	template <class T>
	static T value() {
		return static_cast<T>(42);
	}
};

template <class T>
using HostValue = decltype(Host::template value<T>());

template <class T>
HostValue<T> run() {
	return Host::template value<T>();
}

int main() {
	return run<int>() == 42 ? 0 : 1;
}
