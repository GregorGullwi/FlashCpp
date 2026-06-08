struct Callable {
	int operator()(int x) const {
		return x + 1;
	}
};

using Alias = Callable;

int call_ref(const Alias& callable) {
	return callable(41);
}

int main() {
	Alias callable;
	return call_ref(callable) == 42 ? 0 : 1;
}
