struct Subject {
private:
	template<class Type>
	int pick(Type) requires (sizeof(Type) == sizeof(int));

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
