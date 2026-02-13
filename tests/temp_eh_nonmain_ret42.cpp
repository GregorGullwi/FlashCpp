int result = 1;

void f() {
	try {
		throw 7;
	} catch (...) {
		result = 42;
	}
}

int main() {
	f();
	return result;
}
