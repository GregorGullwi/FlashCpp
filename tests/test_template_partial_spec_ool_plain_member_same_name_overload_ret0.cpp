// Regression: partial-specialization plain out-of-line member attachment must
// resolve through source-member -> instantiated-stub identity so same-name
// overloads attach to the right instantiated declaration.
template<class T>
struct PartialPick;

template<class T>
struct PartialPick<T*> {
	int pick(T*);
	int pick(long*);
};

template<class T>
int PartialPick<T*>::pick(T*) {
	return 2;
}

int main() {
	PartialPick<int*> pick;
	int value = 0;
	if (pick.pick(&value) != 2) {
		return 1;
	}
	return 0;
}

template<class T>
int PartialPick<T*>::pick(long*) {
	return 1;
}
