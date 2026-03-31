int g_dtor_calls = 0;
struct Tracked {
	int id;
	~Tracked() { g_dtor_calls++; }
};
int main() {
	try {
		Tracked t{1};
		throw 42;
	} catch (int e) {
		return (e == 42 && g_dtor_calls == 1) ? 0 : 1;
	}
	return 2;
}
