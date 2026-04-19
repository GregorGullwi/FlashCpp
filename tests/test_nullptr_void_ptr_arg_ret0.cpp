int take_void_ptr(void* stream) {
	return stream == nullptr ? 0 : 1;
}

int take_const_void_ptr(const void* stream) {
	return stream == nullptr ? 0 : 2;
}

int take_int_ptr(int* value) {
	return value == nullptr ? 0 : 4;
}

int main() {
	return take_void_ptr(nullptr) +
		take_const_void_ptr(nullptr) +
		take_int_ptr(nullptr);
}
