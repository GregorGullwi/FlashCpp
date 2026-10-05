// __is_constructible with no constructor arguments asks the
// default-construction question. The record classifier used to report a class
// constructible whenever a default constructor existed, even when it was
// deleted or inaccessible, or when the class was abstract.
struct Plain {
	int value;
	long tag;
};

struct DeletedDefault {
	DeletedDefault() = delete;
};

struct PrivateDefault {
private:
	PrivateDefault();
};

struct ProtectedDefault {
protected:
	ProtectedDefault();
};

struct Abstract {
	virtual void method() = 0;
};

struct NeedsArgument {
	NeedsArgument(int);
};

struct BaseGood {
	int value;
};

struct DerivedGood : BaseGood {
	long tag;
};

template <class Type>
struct Box {
	Type value;
};

static_assert(__is_constructible(Plain), "aggregate is default constructible");
static_assert(!__is_constructible(DeletedDefault), "deleted default is not constructible");
static_assert(!__is_constructible(PrivateDefault), "private default is not constructible");
static_assert(!__is_constructible(ProtectedDefault), "protected default is not constructible");
static_assert(!__is_constructible(Abstract), "abstract is not constructible");
static_assert(!__is_constructible(NeedsArgument), "no default constructor");
static_assert(__is_constructible(BaseGood), "plain base is default constructible");
static_assert(__is_constructible(DerivedGood), "derived is default constructible");
static_assert(__is_constructible(Box<int>), "class-template specialization is default constructible");
static_assert(__is_constructible(int), "int is default constructible");
static_assert(__is_constructible(double*), "pointer is default constructible");
static_assert(!__is_constructible(int&), "reference is not constructible");

int main() {
	return 0;
}
