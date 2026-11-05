// C++20 [temp.deduct.partial]: a dependent NTTP pattern that reuses the same
// parameter across function parameters is more specialized than two independent
// dependent patterns, even though their structural specificity scores tie.
template<unsigned N>
struct Cell {};

template<unsigned N, unsigned M>
int choose(Cell<N>*, Cell<M>*) {
	return 1;
}

template<unsigned N>
int choose(Cell<N>*, Cell<N>*) {
	return 2;
}

int main() {
	Cell<1> cell;
	return choose(&cell, &cell) == 2 ? 42 : 1;
}
