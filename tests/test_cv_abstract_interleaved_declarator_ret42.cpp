int main() {
	int (* const (* volatile p)[3])[4] = nullptr;
	int (*(*incomplete)[3])[] = nullptr;
	const bool cv_shape = sizeof(*p) == 3 * sizeof(int (*)[4]);
	const bool abstract_shape = sizeof(int (*(*)[3])[4]) == sizeof(void*);
	const bool incomplete_pointee_shape =
		sizeof(*incomplete) == 3 * sizeof(int (*)[]);
	return cv_shape && abstract_shape && incomplete_pointee_shape ? 42 : 1;
}
