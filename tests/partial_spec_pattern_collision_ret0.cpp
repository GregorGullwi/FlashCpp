// Test that templates with "_pattern" in their name don't collide
// with the internal pattern naming used for partial specializations.

// ============================================================
// 1. Basic collision test: template name contains "_pattern"
// ============================================================
template <typename T>
struct my_pattern_matcher {
	T value;
	int get() { return 1; }
};

// Partial specialization for pointers - should get unique internal name
template <typename T>
struct my_pattern_matcher<T*> {
	T* value;
	int get() { return 2; }
};

// Partial specialization for references
template <typename T>
struct my_pattern_matcher<T&> {
	T& value;
	int get() { return 3; }
};

// ============================================================
// 2. Friend class access through partial specialization patterns
//    Exercises checkFriendClassAccess step 4 (registry lookup).
// ============================================================
template <typename T>
struct Container;

class Secret {
	int value;
	// Grant friend access to the Container template
	friend struct Container<int>;
	friend struct Container<int*>;

public:
	Secret(int v) : value(v) {}
};

template <typename T>
struct Container {
	T data;
	// Access private member of Secret - requires friend resolution
	int read_secret(Secret& s) { return s.value; }
};

// Partial specialization for pointers - also needs friend access
template <typename T>
struct Container<T*> {
	T* data;
	int read_secret(Secret& s) { return s.value + 10; }
};

// ============================================================
// 3. Member struct template with partial specializations
//    Tests the nested/qualified pattern name path
//    (ParentClass::InnerTemplate$pattern_...)
// ============================================================
struct Outer {
	template <typename T>
	struct Inner {
		T val;
		int id() { return 100; }
	};

	// Partial specialization of a member struct template
	template <typename T>
	struct Inner<T*> {
		T* val;
		int id() { return 200; }
	};
};

// ============================================================
// 4. Another "_pattern" collision: template name is exactly "_pattern"
//    Stress test: the user name is a substring of the old separator.
// ============================================================
template <typename T>
struct _pattern {
	T x;
	int tag() { return 50; }
};

template <typename T>
struct _pattern<T*> {
	T* x;
	int tag() { return 60; }
};

// ============================================================
// 5. Friend access via base template name → exercises
//    checkFriendClassAccess step 4 (registry-based lookup).
//    The friend declaration names "Accessor" (the base template),
//    so the partial specialization pattern struct
//    (e.g., Accessor$pattern_TP) must recover "Accessor" via the
//    registry to match the friend entry.
// ============================================================
template <typename T>
struct Accessor;

class Vault {
	int code;
	// Grant friend access to the *base template name* "Accessor".
	// Partial specialisations must recover this name via the registry.
	friend class Accessor;

public:
	Vault(int c) : code(c) {}
};

template <typename T>
struct Accessor {
	T data;
	int crack(Vault& v) { return v.code; }
};

// Partial specialisation for pointers – internal pattern name is
// Accessor$pattern_TP, which must resolve to "Accessor" to match
// the friend declaration.
template <typename T>
struct Accessor<T*> {
	T* data;
	int crack(Vault& v) { return v.code + 100; }
};

// Partial specialisation for references
template <typename T>
struct Accessor<T&> {
	T& data;
	int crack(Vault& v) { return v.code + 200; }
};

// ============================================================
// 6. Member struct template partial spec with friend access.
//    Exercises the qualified-base-name path in step 4
//    (base name is "Host::Probe", already qualified → skip
//     namespace-prefix prepend).
// ============================================================
struct Host {
	template <typename T>
	struct Probe;

	class Safe {
		int pin;
		friend class Probe;

	public:
		Safe(int p) : pin(p) {}
	};

	template <typename T>
	struct Probe {
		T val;
		int open(Safe& s) { return s.pin; }
	};

	template <typename T>
	struct Probe<T*> {
		T* val;
		int open(Safe& s) { return s.pin + 1000; }
	};
};

int main() {
	int result = 0;

	// --- Test 1: basic collision ---
	my_pattern_matcher<int> a{42};
	int x = 10;
	my_pattern_matcher<int*> b{&x};
	my_pattern_matcher<int&> c{x};

	// Primary: 1, pointer partial spec: 2, reference partial spec: 3
	if (a.get() != 1)
		result |= 1;
	if (b.get() != 2)
		result |= 2;
	if (c.get() != 3)
		result |= 4;

	// --- Test 2: friend access through partial spec (specific instantiation friends) ---
	Secret secret{42};
	Container<int> ci{7};
	if (ci.read_secret(secret) != 42)
		result |= 8;

	Container<int*> cp{&x};
	if (cp.read_secret(secret) != 52)
		result |= 16;  // 42 + 10

	// --- Test 3: member struct template partial spec ---
	Outer::Inner<int> inner_val{5};
	if (inner_val.id() != 100)
		result |= 32;

	Outer::Inner<int*> inner_ptr{&x};
	if (inner_ptr.id() != 200)
		result |= 64;

	// --- Test 4: template named "_pattern" ---
	_pattern<int> p1{99};
	if (p1.tag() != 50)
		result |= 128;

	_pattern<int*> p2{&x};
	if (p2.tag() != 60)
		result |= 256;

	// --- Test 5: friend via base template name (step 4 registry lookup) ---
	Vault vault{77};

	Accessor<int> a5{5};
	if (a5.crack(vault) != 77)
		result |= 512;		   // primary template

	Accessor<int*> a5p{&x};
	if (a5p.crack(vault) != 177)
		result |= 1024;		// pointer partial spec: 77 + 100

	Accessor<int&> a5r{x};
	if (a5r.crack(vault) != 277)
		result |= 2048;		// reference partial spec: 77 + 200

	// --- Test 6: member struct partial spec with friend access ---
	Host::Safe safe{500};

	Host::Probe<int> p6{3};
	if (p6.open(safe) != 500)
		result |= 4096;		// primary member template

	Host::Probe<int*> p6p{&x};
	if (p6p.open(safe) != 1500)
		result |= 8192;		// pointer member partial spec: 500 + 1000

	// All bits clear → return 0
	return result;
}
