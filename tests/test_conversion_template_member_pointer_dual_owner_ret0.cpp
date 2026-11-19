// Conversion-function templates must keep distinct member-pointer owners after
// substitution. Ordinary owners stay separate from each other, specialized owners
// stay distinct across template arguments, and an ordered member-pointer component
// may carry the declaring class by EntityId/TypeId alone without a flat
// has_member_class() spelling for mangling.

struct ObjectOwner {
	int data;
};

struct FunctionOwner {
	int run();
};

template<class T>
struct SpecializedOwner {
	T value;
};

using ObjectPtr = int ObjectOwner::*;
using FunctionPtr = int (FunctionOwner::*)();
using SpecializedIntPtr = int SpecializedOwner<int>::*;
using SpecializedLongPtr = int SpecializedOwner<long>::*;

struct TemplateSource {
	template<class T>
	operator T() const {
		return T();
	}
};

struct ObjectSelection {
	char value;
};

struct FunctionSelection {
	char value;
};

struct SpecializedIntSelection {
	char value;
};

struct SpecializedLongSelection {
	char value;
};

struct EllipsisSelection {
	long long value;
};

ObjectSelection select_object(ObjectPtr);
EllipsisSelection select_object(...);
FunctionSelection select_function(FunctionPtr);
EllipsisSelection select_function(...);
SpecializedIntSelection select_specialized_int(SpecializedIntPtr);
EllipsisSelection select_specialized_int(...);
SpecializedLongSelection select_specialized_long(SpecializedLongPtr);
EllipsisSelection select_specialized_long(...);

static_assert(__is_same(decltype(select_object(TemplateSource{})), ObjectSelection));
static_assert(__is_same(decltype(select_function(TemplateSource{})), FunctionSelection));
static_assert(__is_same(decltype(select_specialized_int(TemplateSource{})), SpecializedIntSelection));
static_assert(__is_same(decltype(select_specialized_long(TemplateSource{})), SpecializedLongSelection));

int consume_object(ObjectPtr pointer) {
	return pointer == nullptr ? 0 : 1;
}

int consume_function(FunctionPtr pointer) {
	return pointer == nullptr ? 0 : 1;
}

// These parameter types keep an ordered member-pointer component whose owner is
// published by identity on the component; mangling must resolve ObjectOwner /
// FunctionOwner from that identity rather than a flat spelling backfill.
int identity_owner_object(int ObjectOwner::* pointer) {
	return pointer == nullptr ? 0 : 1;
}

int identity_owner_function(int (FunctionOwner::* pointer)()) {
	return pointer == nullptr ? 0 : 1;
}

int main() {
	TemplateSource source;
	ObjectPtr object_pointer = source;
	FunctionPtr function_pointer = source;
	SpecializedIntPtr specialized_int = source;
	SpecializedLongPtr specialized_long = source;
	if (object_pointer != nullptr || function_pointer != nullptr ||
		specialized_int != nullptr || specialized_long != nullptr) {
		return 1;
	}
	if (sizeof(select_object(source)) != sizeof(ObjectSelection)) {
		return 2;
	}
	if (sizeof(select_function(source)) != sizeof(FunctionSelection)) {
		return 3;
	}
	if (sizeof(select_specialized_int(source)) != sizeof(SpecializedIntSelection)) {
		return 4;
	}
	if (sizeof(select_specialized_long(source)) != sizeof(SpecializedLongSelection)) {
		return 5;
	}
	if (consume_object(object_pointer) != 0 || consume_function(function_pointer) != 0) {
		return 6;
	}
	if (identity_owner_object(object_pointer) != 0 || identity_owner_function(function_pointer) != 0) {
		return 7;
	}
	return 0;
}
