// A published namespace function template replays this body for each use. The
// body-local typedefs must parse under that template's canonical owner;
// member-template and dependent-member stamping remain deliberately deferred.
template <typename T>
T replay_identity(T value) {
	typedef T ReplayLocal;
	ReplayLocal local = value;
	T* pointer = &local;
	return *pointer;
}

struct Pair {
	short left;
	int right;
};

int main() {
	Pair pair{3, 7};
	return replay_identity(pair.right) + replay_identity(pair.left) - 10;
}
