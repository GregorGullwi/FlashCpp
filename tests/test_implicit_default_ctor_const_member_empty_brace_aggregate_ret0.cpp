struct AggregateWithConstMember {
	const int value;
};

int main() {
	AggregateWithConstMember object{};
	return object.value;
}
