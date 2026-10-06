template <class Type>
concept NonEmpty = sizeof(Type) > 0;

template <class Type>
concept Positive = NonEmpty<Type> && sizeof(Type) > 0;

struct Subject {
private:
	template <Positive Type>
	int pick(Type);

public:
	template <NonEmpty OtherType>
	int pick(OtherType);
};

int main() {
	auto selected = static_cast<int (Subject::*)(int)>(&Subject::pick);
	return 0;
}
