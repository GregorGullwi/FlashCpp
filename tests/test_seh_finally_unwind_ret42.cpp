// Test: __finally handler executes during exception unwinding
// Nested __try/__finally inside __try/__except, with access violation in inner try.
// The __finally must run during the unwind phase before the __except handler.
//
// Expected flow:
// 1. Enter outer __try
// 2. Enter inner __try
// 3. Access violation (dereference null pointer)
// 4. During unwind: inner __finally executes, sets result to 42
// 5. Outer __except catches the exception
// 6. Return result (42)

int main() {
	int result = 0;

	__try {
		__try {
			// Cause an access violation
			int* p = 0;
			*p = 123;
			result = 99; // Should not be reached
		}
		__finally {
			// This MUST execute during unwind
			result = 42;
		}
		result = 99; // Should not be reached
	}
	__except(1) {
		// Exception caught after __finally ran
	}

	return result;
}
