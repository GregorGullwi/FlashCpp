int runtime_value() {
	return 42;
}

void foo() {
	static constinit int x = runtime_value();
}

int main() {
	foo();
	return 0;
}
