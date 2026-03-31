int g = 0;
void helper() {}
template <typename T>
int f(T) {
	helper();
	return g;
}
int main() { return f(42); }
