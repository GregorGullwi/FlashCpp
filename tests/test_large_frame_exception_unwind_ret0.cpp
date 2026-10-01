// Exercise full-width Windows unwind allocation through both prologue forms.
void throwLargeFrameValue(int value) {
	throw value;
}

void largePlainFrame(int seed) {
	char storage[600000];
	storage[0] = (char)seed;
	storage[599999] = (char)(seed + 1);
	throwLargeFrameValue(storage[0] + storage[599999]);
}

void largeCatchFrame(int seed) {
	char storage[600000];
	storage[0] = (char)seed;
	storage[599999] = (char)(seed + 1);
	try {
		throwLargeFrameValue(storage[0]);
	} catch (int value) {
		throwLargeFrameValue(value + storage[599999]);
	}
}

int main() {
	int first = 0;
	int second = 0;
	try { largePlainFrame(7); } catch (int value) { first = value; }
	try { largeCatchFrame(11); } catch (int value) { second = value; }
	return first == 15 && second == 23 ? 0 : 1;
}
