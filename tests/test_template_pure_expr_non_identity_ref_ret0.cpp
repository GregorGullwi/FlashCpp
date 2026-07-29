int plain_global = 7;
int cast_global = 9;

template <class T>
T& return_plain_global(T& ignored) {
	return plain_global;
}

template <class T>
T& return_cast_global(T& ignored) {
	return static_cast<T&>(cast_global);
}

int main() {
	int local = 3;
	return_plain_global<int>(local) = 11;
	return_cast_global<int>(local) = 13;
	return plain_global == 11 && cast_global == 13 && local == 3 ? 0 : 1;
}
