template<class Type>
concept IntSized = sizeof(Type) == sizeof(int);

struct Subject {
private:
	template<IntSized Type>
	int pick(Type);

public:
	template<class Type>
	int pick(Type);
};

int main() {
	auto selected_char = static_cast<int (Subject::*)(char)>(
		&Subject::pick);
	auto selected_int = static_cast<int (Subject::*)(int)>(
		&Subject::pick);
	return 0;
}
