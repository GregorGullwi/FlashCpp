#include "shared.h"

struct PolymorphicImpl : PolymorphicBase {};

int main() {
	PolymorphicImpl object;
	return polymorphicIdentityTag(object) + 42;
}
