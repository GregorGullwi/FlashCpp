// Valid default-argument declarations must keep parsing. The trailing-default
// rule only rejects a non-defaulted parameter that follows a defaulted one, and
// it must not disturb these legal forms.
void f(int a, int b = 2);
void f(int a = 1, int b = 2);

int main() {
	return 42;
}
