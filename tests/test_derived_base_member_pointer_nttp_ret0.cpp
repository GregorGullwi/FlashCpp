struct MemberPointerOwner {
	int value;
	int method() {
		return 7;
	}
};

template<int MemberPointerOwner::*Member>
struct MemberObjectPointerBase {
	int value;
};

struct MemberObjectPointerDerived : MemberObjectPointerBase<&MemberPointerOwner::value> {
	int tag;
};

template<int (MemberPointerOwner::*Method)()>
struct MemberFunctionPointerBase {
	int value;
};

struct MemberFunctionPointerDerived : MemberFunctionPointerBase<&MemberPointerOwner::method> {
	int tag;
};

int main() {
	MemberObjectPointerDerived object{};
	MemberObjectPointerBase<&MemberPointerOwner::value>& object_base = object;
	object_base.value = 0;
	if (object_base.value != 0) {
		return 1;
	}
	MemberFunctionPointerDerived function_object{};
	MemberFunctionPointerBase<&MemberPointerOwner::method>& function_base = function_object;
	function_base.value = 0;
	return function_base.value;
}
