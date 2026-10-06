template <class Type, class Marker>
concept IsIntAndCharacter =
	sizeof(Type) == sizeof(int) && sizeof(Marker) == sizeof(char);

struct Subject {
private:
	template <IsIntAndCharacter<char> Type>
	int pick(Type);

public:
	template <IsIntAndCharacter<int> OtherType>
	int pick(OtherType);
};

int main() {
	auto selected = static_cast<int (Subject::*)(int)>(&Subject::pick);
	return 0;
}
