#include "shared.h"

int firstTypeidTemplateValue() {
	TypeidTemplateBase object;
	return typeidTemplateTag(0) + static_cast<int>(typeid(object) == typeid(TypeidTemplateBase));
}
