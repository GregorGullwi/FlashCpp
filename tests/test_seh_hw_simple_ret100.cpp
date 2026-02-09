// Simple test: catch a null pointer dereference
// Expected return: 100

#define EXCEPTION_EXECUTE_HANDLER 1

int main() {
	__try {
		int* null_ptr = 0;
		*null_ptr = 42;  // Access violation
		return 0;  // Should not reach here
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		return 100;  // Should execute this
	}
}
