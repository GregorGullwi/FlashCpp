// A type-trait operand is a declarator specifier that the parser materializes.
// When the operand names a record or an enum, that specifier must carry the
// nominal EntityId so the canonical type table imports it as a Record/Enum node
// instead of deferring to the compatibility classifier. Both operand spellings
// reach a different materialization site - a bare type-id and a `decltype` - and
// both must publish the same identity, so the same trait must answer the same
// way through either.
struct Widget {
	int value;
	long tag;
	void bump();
};

union Variant {
	int narrow;
	long wide;
};

enum class Scoped : unsigned short { A = 1 };
enum Plain { B = 2 };

template <typename T>
struct Box {
	T value;
	T* pointer;
};

void Widget::bump() {}

// A bare type-id operand: the specifier comes from the trait's own type-id
// parse. `Widget` and `Variant` are class types, so they are objects and
// compounds but not scalars and not enums.
static_assert(__is_object(Widget));
static_assert(__is_compound(Widget));
static_assert(!__is_scalar(Widget));
static_assert(!__is_enum(Widget));

static_assert(__is_object(Variant));
static_assert(__is_compound(Variant));
static_assert(!__is_scalar(Variant));
static_assert(!__is_enum(Variant));

// Both enum forms are enums, scalars, objects, and compounds. Only the
// canonical enum EntityId distinguishes one enum from another, so a flat
// type-index comparison cannot answer this pair.
static_assert(__is_enum(Scoped));
static_assert(__is_enum(Plain));
static_assert(__is_scalar(Scoped));
static_assert(__is_object(Scoped));
static_assert(__is_compound(Plain));

// `__is_same` between two nominal operands needs the same published identity.
static_assert(__is_same(Widget, Widget));
static_assert(__is_same(Scoped, Scoped));
static_assert(!__is_same(Widget, Variant));
static_assert(!__is_same(Scoped, Plain));
static_assert(!__is_same(Widget, int));

// A class-template specialization is a complete class type.
static_assert(__is_object(Box<int>));
static_assert(__is_compound(Box<int>));
static_assert(!__is_scalar(Box<int>));

// A `decltype` operand: the specifier is minted by the decltype builder from the
// operand expression's type, which is a different materialization site.
Widget widget{};
Widget* widgetPointer = &widget;
Scoped scoped = Scoped::A;

static_assert(__is_object(decltype(widget)));
static_assert(__is_compound(decltype(widget)));
static_assert(!__is_scalar(decltype(widget)));
static_assert(__is_enum(decltype(scoped)));
static_assert(__is_pointer(decltype(widgetPointer)));
static_assert(__is_scalar(decltype(widgetPointer)));

// The two spellings of one nominal type must agree.
static_assert(__is_same(Widget, decltype(widget)));
static_assert(__is_same(Scoped, decltype(scoped)));
static_assert(__is_same(Widget*, decltype(widgetPointer)));

// A member-data pointer needs the declaring class as its owner identity, so it
// is a member-object pointer for a record owner and never a plain pointer. The
// trait argument parser does not accept a member-pointer type-id directly, so
// the operand is spelled through a declaration.
int Widget::* memberPointer = &Widget::value;

static_assert(__is_member_object_pointer(decltype(memberPointer)));
static_assert(!__is_pointer(decltype(memberPointer)));
static_assert(__is_scalar(decltype(memberPointer)));

int main() {
	// The same classification must hold when the trait is lowered as ordinary
	// code instead of folded as a constant expression.
	unsigned mismatches = 0;
	mismatches |= __is_object(Widget) ? 0u : 1u;
	mismatches |= __is_compound(Variant) ? 0u : 2u;
	mismatches |= __is_scalar(Widget) ? 4u : 0u;
	mismatches |= __is_enum(Scoped) ? 0u : 8u;
	mismatches |= __is_enum(Plain) ? 0u : 16u;
	mismatches |= __is_same(Widget, Variant) ? 32u : 0u;
	mismatches |= __is_same(Scoped, Plain) ? 64u : 0u;
	mismatches |= __is_object(Box<int>) ? 0u : 128u;
	mismatches |= __is_object(decltype(widget)) ? 0u : 256u;
	mismatches |= __is_enum(decltype(scoped)) ? 0u : 512u;
	mismatches |= __is_scalar(decltype(widgetPointer)) ? 0u : 1024u;
	mismatches |= __is_same(Widget, decltype(widget)) ? 2048u : 0u;
	mismatches |= __is_member_object_pointer(decltype(memberPointer)) ? 0u : 4096u;
	mismatches |= __is_pointer(decltype(memberPointer)) ? 8192u : 0u;
	return static_cast<int>(mismatches & 0x7fffu);
}
