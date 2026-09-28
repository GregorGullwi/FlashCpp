template <class Type>
struct Callable {
	int run() noexcept(sizeof(Type) == sizeof(int)) {
		return 0;
	}
};

using IntCallable = Callable<int>;
using CharCallable = Callable<char>;

struct NoexceptSelection { char marker[1]; };
struct ThrowingSelection { char marker[2]; };

NoexceptSelection choose(int (IntCallable::*)() noexcept);
ThrowingSelection choose(int (IntCallable::*)());

NoexceptSelection choose(int (CharCallable::*)() noexcept);
ThrowingSelection choose(int (CharCallable::*)());

template <class Type>
using SelectedMemberOverload = decltype(choose(&Callable<Type>::run));

static_assert(sizeof(SelectedMemberOverload<int>) == sizeof(NoexceptSelection));
static_assert(sizeof(SelectedMemberOverload<char>) == sizeof(ThrowingSelection));

int main() {
	return 0;
}
