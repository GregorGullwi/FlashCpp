template <typename T>
int forward_value(T&& value) {
	return value;
}

int main() {
	int x = 17;
	if (forward_value(x) != 17)
		return 1;
	if (forward_value(23) != 23)
		return 2;
	return 0;
}
