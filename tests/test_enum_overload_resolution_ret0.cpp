// Regression test: enum overload resolution must preserve enum identity and
// prefer the enum overload over the integer overload.
// Per C++20 [over.ics.rank], exact match (enum->enum) is preferred
// over promotion (enum->int).

enum Color { Red = 1,
			 Green = 2,
			 Blue = 3 };

int overloaded(int) { return 1; }
int overloaded(Color) { return 2; }

int main() {
	Color c = Green;
	int result = overloaded(c);

	int i = 5;
	int result2 = overloaded(i);

	return (result == 2 && result2 == 1) ? 0 : 1;
}
