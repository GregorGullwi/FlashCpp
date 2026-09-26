// Parser-side overload ranking must plan conversions for non-projectable
// ordered pointer types from their canonical structural identities.
int choose_pointer(void*) {
	return 42;
}

int choose_pointer(long) {
	return 7;
}

int (*(*ordered_pointer)[3])[4] = nullptr;

int main() {
	return choose_pointer(ordered_pointer);
}
