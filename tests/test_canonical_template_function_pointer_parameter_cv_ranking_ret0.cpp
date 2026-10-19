struct Payload {
	long value;
};

using Callback = long (*)(int, Payload);

template <class Type>
using ConstParameterCallback = long (*)(const Type, const Payload);

int choose(Callback) {
	return 17;
}

int choose(...) {
	return 29;
}

long target(int count, Payload payload) {
	return count + payload.value;
}

int main() {
	ConstParameterCallback<int> callback = &target;
	return choose(callback) == 17 ? 0 : 1;
}
