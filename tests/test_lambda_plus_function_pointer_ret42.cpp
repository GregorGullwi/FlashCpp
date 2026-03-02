int call_fp(int (*fp)(int)) {
	return fp(41);
}

int main() {
	auto fp = +[](int v) { return v + 1; };
	return call_fp(fp);
}
