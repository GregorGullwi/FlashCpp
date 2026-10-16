// A calling convention written at the head of a parenthesized declarator belongs
// to the function-pointer type (`void (__vectorcall* p)(Packet)`), so it must
// survive into the declared type's identity instead of collapsing to the default.
// The parameter list of the trailing `(Packet)` used to reset the parser's
// pending calling convention, dropping it from the signature.
struct Packet {
	short tag;
	int value;
};

using VectorcallCallback = void(__vectorcall*)(Packet);
using CdeclCallback = void(__cdecl*)(Packet);

void (__vectorcall* g_vectorcall)(Packet);
void (__cdecl* g_cdecl)(Packet);

static_assert(!__is_same(VectorcallCallback, CdeclCallback));
static_assert(!__is_same(void(__vectorcall*)(Packet), void(__cdecl*)(Packet)));
static_assert(__is_same(decltype(g_vectorcall), VectorcallCallback));
static_assert(__is_same(decltype(g_cdecl), CdeclCallback));

void verify_vectorcall(void (__vectorcall* callback)(Packet)) {
	static_assert(__is_same(decltype(callback), VectorcallCallback));
}

void verify_cdecl(void (__cdecl* callback)(Packet)) {
	static_assert(__is_same(decltype(callback), CdeclCallback));
}

int main() { return 0; }
