#include "shared.h"

template int bump<int>(int);
template class StrongBox<int>;

int main() {
	return firstBump() + firstBox() + bump(short(11));
}
