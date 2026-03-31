int sumRestricted(int* __restrict lhs, int const* __restrict rhs) {
	return *lhs + *rhs;
}

int main() {
#ifdef __restrict
	return 1;
#else
	int lhs = 20;
	int rhs = 22;
	int* __restrict lhsPtr = &lhs;
	return sumRestricted(lhsPtr, &rhs);
#endif
}
