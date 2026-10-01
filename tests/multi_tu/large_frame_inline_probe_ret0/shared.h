inline int largeSharedFrame(int input) {
	char storage[70000];
	storage[0] = (char)input;
	storage[69999] = (char)(input + 1);
	return storage[0] + storage[69999];
}
int firstLargeFrame();
