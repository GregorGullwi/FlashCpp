int target_a() {
	return 21;
}

int target_b() {
	return 22;
}

template <int (*F)()>
int call_function() {
	return F();
}

int main() {
	if (call_function<&target_a>() != 21) return 1;
	if (call_function<&target_b>() != 22) return 2;
	return 0;
}
