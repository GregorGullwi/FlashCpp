// A fixed non-type template argument is more specialized than a dependent one.
template<unsigned N> struct Cell {};
template<unsigned N, class T> int choose(Cell<N>*, T*) { return 1; }
template<class T> int choose(Cell<1u>*, T*) { return 2; }
int main() {
	Cell<1u>* cell = nullptr;
	int value = 0;
	return choose(cell, &value) == 2 ? 0 : 1;
}
