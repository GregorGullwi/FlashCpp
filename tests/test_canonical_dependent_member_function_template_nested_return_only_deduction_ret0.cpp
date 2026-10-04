template<class Type>
struct ResultWrapper;

template<class Type>
struct Owner {
	template<class Result>
	ResultWrapper<Result> run(int) const &;
};

struct ScalarSelection {
	char marker[1];
};

struct CharacterSelection {
	char marker[2];
};

ScalarSelection choose(ResultWrapper<int> (Owner<int>::*)(int) const &);
CharacterSelection choose(ResultWrapper<char> (Owner<char>::*)(int) const &);

template<class Type>
using OwnerSelection = decltype(choose(&Owner<Type>::template run<>));

static_assert(__is_same(OwnerSelection<int>, ScalarSelection));
static_assert(__is_same(OwnerSelection<char>, CharacterSelection));

int main() {
	return 0;
}
