// Regression: published class EntityId must bind onto Class::* declarators so
// member-object pointers import as canonical types without StringHandle identity.
struct Owner {
	int field;
};

int read_field(const Owner& value, int Owner::* member) {
	return value.*member;
}

int main() {
	Owner owner{42};
	return read_field(owner, &Owner::field) - 42;
}
