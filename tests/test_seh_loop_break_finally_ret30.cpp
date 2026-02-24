// Test break inside __try with __finally in a loop
// __finally MUST execute even when break is used

int main() {
	int result = 0;
	for (int i = 0; i < 10; i++) {
		__try {
			result = result + 10;
			if (i == 1) {
				break;  // break on second iteration
			}
		}
		__finally {
			result = result + 5;  // must run even on break
		}
	}
	// i=0: result = 0+10+5 = 15
	// i=1: result = 15+10 = 25, break, __finally: 25+5 = 30...
	// Actually: break should trigger __finally
	// Hmm but does FlashCpp emit SehFinallyCall before break?
	// i=0: +10 +5 = 15
	// i=1: +10 = 25, break triggers __finally: +5 = 30
	// Wait, that's 30 not 25. Let me rethink.
	// Actually with __finally: result = 30
	return result;  // expect 30
}
