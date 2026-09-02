#include "shared.h"

template int bump<int>(int);
template class StrongBox<int>;

int firstBump() {
	return bump(40);
}

int firstBox() {
	StrongBox<int> box;
	box.set(1);
	return box.get();
}
