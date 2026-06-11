// Regression: out-of-line class-template member replay can materialize a
// dependent unqualified direct call that later gets resolved to the
// definition-bound ordinary overload. Keep that binding through
// replay/materialization instead of relying on late recovery.

int lookup_probe(long) {
	return 1;
}

template <typename T>
struct Box {
	int run();
};

template <typename T>
int Box<T>::run() {
	return lookup_probe(T{});
}

int lookup_probe(int) {
	return 2;
}

int main() {
	return Box<int>{}.run() - 1;
}
