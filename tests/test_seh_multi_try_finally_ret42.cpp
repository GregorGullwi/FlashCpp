// Test multiple __try/__finally blocks in one function

int main() {
	int result = 0;

	__try {
		result = result + 10;
	}
	__finally {
		result = result + 5;  // 15
	}

	__try {
		result = result + 10;
	}
	__finally {
		result = result + 5;  // 30
	}

	__try {
		result = result + 7;
	}
	__finally {
		result = result + 5;  // 42
	}

	return result;  // expect 42
}
