// An alias denotes its underlying struct, so a forward declaration spelled with
// the struct and a definition spelled with the alias are the same function
// template. Deciding by canonical identity must not split that redeclaration
// into two overloads, which would leave the call without a definition.
struct Record {
	int value;
};

using RecordAlias = Record;

template <class T>
int take(T tag, Record value);

template <class T>
int take(T tag, RecordAlias value) {
	return value.value;
}

int main() {
	Record record{42};
	if (take(0, record) != 42) {
		return 1;
	}
	return 42;
}
