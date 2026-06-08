template<typename Callable>
int invokeConst(const Callable& callable) {
	return callable.operator()(1);
}

struct MutableOnlyCallable {
	int operator()(int value) {
		return value + 1;
	}
};

int main() {
	MutableOnlyCallable callable;
	return invokeConst(callable);
}
