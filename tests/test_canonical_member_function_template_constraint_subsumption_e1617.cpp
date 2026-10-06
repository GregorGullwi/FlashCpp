template <class Type>
concept NonEmpty = sizeof(Type) > 0;

template <class Type>
concept Positive = NonEmpty<Type> && sizeof(Type) > 0;

struct Subject {
private:
	template <class Type>
	int pick(Type) requires Positive<Type>;

public:
	template <class OtherType>
	int pick(OtherType) requires NonEmpty<OtherType>;
};

int main() {
	auto selected = static_cast<int (Subject::*)(int)>(&Subject::pick);
	return 0;
}
