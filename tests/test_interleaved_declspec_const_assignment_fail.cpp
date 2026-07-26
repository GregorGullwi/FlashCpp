const __declspec(align(16)) int immutable_value = 42;

int main() {
	immutable_value = 0;
	return immutable_value;
}
