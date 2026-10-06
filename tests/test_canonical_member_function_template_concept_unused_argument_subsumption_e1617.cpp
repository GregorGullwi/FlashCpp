template <class Type, class Marker>
concept NonEmpty = sizeof(Type) > 0;

template <class Type>
concept Positive = NonEmpty<Type, int> && sizeof(Type) > 0;

struct Subject {
private:
	template <Positive Type>
	int pick(Type);

public:
	template <NonEmpty<char> OtherType>
	int pick(OtherType);
};

int main() {
	auto selected = static_cast<int (Subject::*)(int)>(&Subject::pick);
	return 0;
}
