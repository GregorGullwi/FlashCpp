template<class T>
struct Accessor;

template<class T>
class Host {
	int value = 42;

	friend struct Accessor<T>;
};

template<class T>
struct Accessor {
	int readIntHost(Host<int>& host) {
		return host.value;
	}
};

int main() {
	Host<int> host;
	Accessor<double> accessor;
	return accessor.readIntHost(host);
}
