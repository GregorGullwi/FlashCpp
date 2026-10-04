void* preserve(void* value, int guard) {
	if (guard != 17) return nullptr;
	return value;
}

int main() {
	int value = 41;
	return preserve(&value, 17) == &value ? 0 : 1;
}