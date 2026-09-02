#include "shared.h"

struct TemplateImpl : PolymorphicTemplate<int> {};

int main() {
	TemplateImpl object;
	return polymorphicTemplateIdentityTag(object, 5)
		+ static_cast<int>(typeid(object) != typeid(PolymorphicTemplate<int>))
		+ 40;
}
