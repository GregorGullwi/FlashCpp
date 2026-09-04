#include "shared.h"

struct TypeidTemplateDerived : TypeidTemplateBase {};

int main() {
	TypeidTemplateDerived object;
	return firstTypeidTemplateValue() + typeidTemplateTag(0) +
		static_cast<int>(typeid(object) != typeid(TypeidTemplateBase)) + 44;
}
