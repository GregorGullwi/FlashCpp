#if defined(_MSC_VER)
struct Packet {
	short tag;
	int value;
};

template <class Type>
struct CallbackOwner {
	void __vectorcall vectorcall_method(Type) {}
	void __cdecl cdecl_method(Type) {}
};

using VectorcallMemberCallback =
	void (__vectorcall CallbackOwner<Packet>::*)(Packet);
using CdeclMemberCallback =
	void (__cdecl CallbackOwner<Packet>::*)(Packet);

struct VectorcallSelection { char marker[3]; };
struct CdeclSelection { char marker[7]; };

VectorcallSelection select_member_callback(VectorcallMemberCallback) {
	return {};
}

CdeclSelection select_member_callback(CdeclMemberCallback) {
	return {};
}

static_assert(!__is_same(VectorcallMemberCallback, CdeclMemberCallback));
static_assert(sizeof(decltype(select_member_callback(
	static_cast<VectorcallMemberCallback>(nullptr)))) ==
	sizeof(VectorcallSelection));
static_assert(sizeof(decltype(select_member_callback(
	static_cast<CdeclMemberCallback>(nullptr)))) ==
	sizeof(CdeclSelection));

VectorcallSelection select_vectorcall_lvalue(
	VectorcallMemberCallback callback) {
	return select_member_callback(callback);
}

CdeclSelection select_cdecl_lvalue(CdeclMemberCallback callback) {
	return select_member_callback(callback);
}

template <class Type>
auto select_vectorcall_after_substitution(
	void (__vectorcall CallbackOwner<Type>::*callback)(Type)) {
	static_assert(__is_same(
		decltype(callback),
		void (__vectorcall CallbackOwner<Type>::*)(Type)));
	return select_member_callback(callback);
}

template <class Type>
auto select_cdecl_after_substitution(
	void (__cdecl CallbackOwner<Type>::*callback)(Type)) {
	static_assert(__is_same(
		decltype(callback),
		void (__cdecl CallbackOwner<Type>::*)(Type)));
	return select_member_callback(callback);
}

static_assert(sizeof(decltype(
	select_vectorcall_after_substitution<Packet>(
		&CallbackOwner<Packet>::vectorcall_method))) ==
	sizeof(VectorcallSelection));
static_assert(sizeof(decltype(
	select_cdecl_after_substitution<Packet>(
		&CallbackOwner<Packet>::cdecl_method))) ==
	sizeof(CdeclSelection));

int main() {
	return 0;
}
#else
int main() {
	return 0;
}
#endif
