template <class Type>
concept First = sizeof(Type) > 0;

template <class Type>
concept Second = sizeof(Type) > 0;

struct Subject {
	template <class Type>
	int pick(Type) requires First<Type>;

	template <class OtherType>
	int pick(OtherType) requires Second<OtherType>;
};

int main() {
	auto selected = static_cast<int (Subject::*)(int)>(&Subject::pick);
	return 0;
}
