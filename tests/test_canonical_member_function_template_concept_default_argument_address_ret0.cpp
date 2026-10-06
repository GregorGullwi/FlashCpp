template <class Type, class Marker = int>
concept SmallerThanCharacter = sizeof(Type) < sizeof(char);

struct Subject {
private:
	template <SmallerThanCharacter Type>
	int pick(Type);

public:
	template <class OtherType>
	int pick(OtherType);
};

int main() {
	auto selected = static_cast<int (Subject::*)(int)>(&Subject::pick);
	return 0;
}
