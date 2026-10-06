template <class Type>
concept NonEmpty = sizeof(Type) > 0;

template <class Type>
concept Positive = NonEmpty<Type> && sizeof(Type) > 0;

struct Subject {
	template <class Type>
	int pick(Type) requires NonEmpty<Type> {
		return 1;
	}

	template <class OtherType>
	int pick(OtherType) requires Positive<OtherType> {
		return 2;
	}
};

using PickPointer = int (Subject::*)(int);
using RawPick = int (*)(Subject*, int);

int main() {
	Subject subject;
	PickPointer selected = static_cast<PickPointer>(&Subject::pick);
	RawPick raw = reinterpret_cast<RawPick>(selected);
	return raw(&subject, 0) == 2 ? 0 : 1;
}
