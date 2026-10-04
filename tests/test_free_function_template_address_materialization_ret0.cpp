template<class Type>
int invoke(Type value) {
	return sizeof(Type) + value;
}

int consume(int (*callback)(int)) {
	return callback(7);
}

template<class Type>
int invoke_overloaded(Type) {
	return 1;
}

template<class Type>
int invoke_overloaded(Type*) {
	return 2;
}

int consume_pointer(int (*callback)(int*)) {
	int value = 5;
	return callback(&value);
}

int main() {
	return consume(&invoke) == 11 &&
			consume_pointer(&invoke_overloaded) == 2
		? 0
		: 1;
}
