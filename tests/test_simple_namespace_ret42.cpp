namespace A {
int func() { return 42; }
} // namespace A
int main() {
	int x = A::func();
	return x;
}
