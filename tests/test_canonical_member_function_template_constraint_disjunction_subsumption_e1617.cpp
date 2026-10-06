template <class Type>
concept Small = sizeof(Type) < 8;

template <class Type>
concept IntSized = sizeof(Type) == sizeof(int);

template <class Type>
concept Weak = Small<Type> || IntSized<Type>;

template <class Type>
concept Strong = (Small<Type> && sizeof(Type) > 0) || IntSized<Type>;

struct Subject {
private:
	template <class Type>
	int pick(Type) requires Strong<Type>;

public:
	template <class OtherType>
	int pick(OtherType) requires Weak<OtherType>;
};

int main() {
	auto selected = static_cast<int (Subject::*)(int)>(&Subject::pick);
	return 0;
}
