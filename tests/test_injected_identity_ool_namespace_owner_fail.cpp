// The right::Box definition belongs only to right::Box. If a leaf-name
// registry collision attaches it to left::Box, the assertion incorrectly
// succeeds because the two owners have different sizes.
namespace left {
template <class T>
struct Box {
	char storage[sizeof(T)];
	using owner_marker = int;
	int self_size();
};
}

namespace right {
template <class T>
struct Box {
	long long storage;
	int self_size();
};
}

namespace right {
template <class T>
int Box<T>::self_size() {
	return sizeof(typename Box::owner_marker);
}
}

int main() {
	right::Box<int> value;
	return value.self_size();
}
