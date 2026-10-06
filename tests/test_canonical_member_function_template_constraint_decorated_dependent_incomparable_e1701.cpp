template <class Type>
concept NonEmpty = sizeof(Type) > 0;

struct Subject {
	template <class Type>
	int pick(Type) requires NonEmpty<Type>;

	template <class Type>
	int pick(Type) requires NonEmpty<Type*>;
};

int main() {
	auto selected = static_cast<int (Subject::*)(int)>(&Subject::pick);
	return 0;
}
