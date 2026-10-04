// C++20 class-property traits use canonical facts for constant evaluation,
// runtime lowering, and lazy concept constraints.
struct PolymorphicRecord {
	virtual void run() {}
};

struct FinalRecord final {};

union FinalUnion final {
	int value;
};

struct AbstractRecord {
	virtual void run() = 0;
};

struct ConcreteRecord : AbstractRecord {
	void run() override {}
};

struct InheritedAbstractRecord : AbstractRecord {};

template <typename T>
struct PolymorphicSpecialization {
	virtual void run() {}
	int value;
};

template <typename T>
struct FinalSpecialization final {};

template <typename T>
struct AbstractSpecialization {
	virtual void run() = 0;
	int value;
};

template <typename T>
struct ConcreteSpecialization : AbstractRecord {
	void run() override {}
};

static_assert(__is_polymorphic(PolymorphicRecord));
static_assert(__is_polymorphic(ConcreteRecord));
static_assert(!__is_polymorphic(FinalRecord));
static_assert(__is_abstract(AbstractRecord));
static_assert(!__is_abstract(ConcreteRecord));
static_assert(__is_abstract(InheritedAbstractRecord));
static_assert(__is_final(FinalRecord));
static_assert(__is_final(FinalUnion));
static_assert(!__is_final(ConcreteRecord));
static_assert(sizeof(PolymorphicSpecialization<int>) == 16);
static_assert(__is_polymorphic(PolymorphicSpecialization<int>));
static_assert(__is_final(FinalSpecialization<int>));
static_assert(sizeof(AbstractSpecialization<int>) == 16);
static_assert(__is_abstract(AbstractSpecialization<int>));
static_assert(__is_polymorphic(ConcreteSpecialization<int>));
static_assert(!__is_abstract(ConcreteSpecialization<int>));
static_assert(!__is_polymorphic(PolymorphicRecord&));
static_assert(!__is_abstract(AbstractRecord*));
static_assert(!__is_final(FinalRecord&));

template <typename T>
concept PolymorphicType = __is_polymorphic(T);

template <typename T>
concept FinalType = __is_final(T);

template <typename T>
concept AbstractType = __is_abstract(T);

template <PolymorphicType T>
int selectPolymorphic(T*) { return 1; }
int selectPolymorphic(...) { return 2; }

template <FinalType T>
int selectFinal(T*) { return 3; }
int selectFinal(...) { return 4; }

template <AbstractType T>
int selectAbstract(T*) { return 5; }
int selectAbstract(...) { return 6; }

int main() {
	PolymorphicRecord* polymorphic = nullptr;
	FinalRecord* final_record = nullptr;
	FinalUnion* final_union = nullptr;
	AbstractRecord* abstract_record = nullptr;
	ConcreteRecord* concrete_record = nullptr;
	InheritedAbstractRecord* inherited_abstract = nullptr;
	PolymorphicSpecialization<int>* polymorphic_specialization = nullptr;
	FinalSpecialization<int>* final_specialization = nullptr;
	AbstractSpecialization<int>* abstract_specialization = nullptr;
	ConcreteSpecialization<int>* concrete_specialization = nullptr;
	unsigned mismatches = 0;
	mismatches |= selectPolymorphic(polymorphic) == 1 ? 0u : 1u;
	mismatches |= selectPolymorphic(concrete_record) == 1 ? 0u : 2u;
	mismatches |= selectPolymorphic(final_record) == 2 ? 0u : 4u;
	mismatches |= selectPolymorphic(polymorphic_specialization) == 1 ? 0u : 256u;
	mismatches |= selectPolymorphic(concrete_specialization) == 1 ? 0u : 512u;
	mismatches |= selectFinal(final_record) == 3 ? 0u : 8u;
	mismatches |= selectFinal(final_union) == 3 ? 0u : 16u;
	mismatches |= selectFinal(polymorphic) == 4 ? 0u : 32u;
	mismatches |= selectFinal(final_specialization) == 3 ? 0u : 1024u;
	mismatches |= selectAbstract(abstract_record) == 5 ? 0u : 64u;
	mismatches |= selectAbstract(concrete_record) == 6 ? 0u : 128u;
	mismatches |= selectAbstract(inherited_abstract) == 5 ? 0u : 2048u;
	mismatches |= selectAbstract(abstract_specialization) == 5 ? 0u : 4096u;
	mismatches |= selectAbstract(concrete_specialization) == 6 ? 0u : 8192u;
	return static_cast<int>(mismatches);
}
