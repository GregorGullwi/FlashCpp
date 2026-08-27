// C++20 [class.member.lookup]: two distinct base members with the same name
// make the member access ill-formed, so substitution selects the primary.

struct Left {
	int value = 11;
};

struct Right {
	int value = 31;
};

struct Derived : Left, Right {};

template <class T>
concept HasUniqueValue = requires(T object) { object.value; };

int main() {
	return HasUniqueValue<Derived> ? 1 : 0;
}
