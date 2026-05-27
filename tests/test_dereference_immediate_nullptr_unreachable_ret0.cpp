int main() {
	if (false) {
		return *static_cast<int*>(0);
	}
	if (false) {
		return *reinterpret_cast<int*>(0x1000ULL);
	}
	return 0;
}
