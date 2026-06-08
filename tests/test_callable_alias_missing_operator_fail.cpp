struct NotCallable {};

using Alias = NotCallable;

int main() {
	Alias a;
	return a(1);
}
