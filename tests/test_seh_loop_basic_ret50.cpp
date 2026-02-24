// Test __try/__except inside a for loop
// Each iteration catches an access violation, accumulating a result

#define EXCEPTION_EXECUTE_HANDLER 1

int main() {
	int result = 0;
	for (int i = 0; i < 5; i++) {
		__try {
			int* p = 0;
			*p = 42;  // access violation every iteration
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			result = result + 10;  // 10 per iteration = 50
		}
	}
	return result;  // expect 50
}
