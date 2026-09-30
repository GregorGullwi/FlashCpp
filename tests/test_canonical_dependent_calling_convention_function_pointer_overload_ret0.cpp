#if defined(_MSC_VER)
struct Packet {
	short tag;
	int value;
};

using VectorcallCallback = void(__vectorcall*)(Packet);
using CdeclCallback = void(__cdecl*)(Packet);

struct VectorcallSelection { char marker[3]; };
struct CdeclSelection { char marker[7]; };

VectorcallSelection select_callback(VectorcallCallback) {
	return {};
}

CdeclSelection select_callback(CdeclCallback) {
	return {};
}

void __vectorcall invoke_vectorcall(Packet) {}
void __cdecl invoke_cdecl(Packet) {}

void verify_nondependent_parameter(
	void (__vectorcall* callback)(Packet)) {
	static_assert(__is_same(decltype(callback), VectorcallCallback));
}

template <class Callback>
auto select_after_substitution(Callback callback) {
	return select_callback(callback);
}

template <class Type>
auto select_vectorcall_after_substitution(
	void (__vectorcall* callback)(Type)) {
	static_assert(__is_same(decltype(callback), VectorcallCallback));
	return select_callback(callback);
}

template <class Type>
auto select_cdecl_after_substitution(void (__cdecl* callback)(Type)) {
	static_assert(__is_same(decltype(callback), CdeclCallback));
	return select_callback(callback);
}

static_assert(sizeof(decltype(select_after_substitution(&invoke_vectorcall))) ==
	sizeof(VectorcallSelection));
static_assert(sizeof(decltype(select_after_substitution(&invoke_cdecl))) ==
	sizeof(CdeclSelection));
static_assert(sizeof(decltype(
	select_vectorcall_after_substitution<Packet>(&invoke_vectorcall))) ==
	sizeof(VectorcallSelection));
static_assert(sizeof(decltype(
	select_cdecl_after_substitution<Packet>(&invoke_cdecl))) ==
	sizeof(CdeclSelection));

int main() {
	return 0;
}
#else
int main() {
	return 0;
}
#endif
