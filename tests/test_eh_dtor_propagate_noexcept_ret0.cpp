// Test: C++20 [except.spec]/7 implicit noexcept propagation through base/member destructors.
// An implicit/defaulted destructor is noexcept unless any base class or non-static
// data member type has a noexcept(false) destructor.

struct ThrowingBase {
	int value;
	~ThrowingBase() noexcept(false) {}  // explicitly may throw
};

struct DerivedImplicit : ThrowingBase {
 // No explicit destructor — implicit dtor inherits noexcept(false) from ThrowingBase
};

struct MemberHolder {
	ThrowingBase member;	 // member has noexcept(false) destructor
 // No explicit destructor — implicit dtor inherits noexcept(false) from member
};

struct SafeBase {
	int value;
	~SafeBase() {}  // noexcept(true) by default per C++11 [class.dtor]/3
};

struct DerivedSafe : SafeBase {
 // implicit dtor remains noexcept(true) since SafeBase is nothrow-destructible
};

int main() {
	int result = 0;

 // DerivedImplicit: implicit dtor inherits noexcept(false) from base
	if (noexcept(DerivedImplicit{}.~DerivedImplicit()))
		result |= 1;
	if (__is_nothrow_destructible(DerivedImplicit))
		result |= 2;

 // MemberHolder: implicit dtor inherits noexcept(false) from member
	if (noexcept(MemberHolder{}.~MemberHolder()))
		result |= 4;
	if (__is_nothrow_destructible(MemberHolder))
		result |= 8;

 // DerivedSafe: implicit dtor remains noexcept(true)
	if (!noexcept(DerivedSafe{}.~DerivedSafe()))
		result |= 16;
	if (!__is_nothrow_destructible(DerivedSafe))
		result |= 32;

 // SafeBase: explicit dtor is noexcept(true) by default
	if (!__is_nothrow_destructible(SafeBase))
		result |= 64;

	return result;
}
