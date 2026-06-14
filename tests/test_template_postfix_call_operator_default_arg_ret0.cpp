// Regression: postfix operator() calls inside template bodies must keep the
// concrete receiver-call path intact, including default arguments.

template <class T>
struct Callable {
	int operator()(int value = 1) const {
		return value;
	}
};

template <class T>
int runPostfixCallable() {
	Callable<T> callable;
	return callable() - 1;
}

int main() {
	return runPostfixCallable<int>();
}
