// A calling convention may appear between the return type and parameter list
// of a bare function type alias.

using Function = int __cdecl(int);

int addOne(int value) {
	return value + 1;
}

int main() {
	Function* invoke = &addOne;
	return invoke(41);
}
