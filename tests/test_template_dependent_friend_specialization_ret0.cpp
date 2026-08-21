template<class T>
struct Accessor;

template<class T>
class Host {
	int value = 42;

	friend struct Accessor<T>;
};

template<class T>
struct Accessor {
	int read(Host<T>& host) {
		return host.value;
	}
};

int main() {
	Host<int> host;
	Accessor<int> accessor;
	return accessor.read(host) == 42 ? 0 : 1;
}
