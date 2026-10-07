// An interleaved member function pointer whose owner is a template parameter
// (`int* (T::*)() const`) is an ordered spine [MemberFunctionPointer, Pointer].
// The owner is not the type token, so substitution must publish the concrete
// class before semantic canonicalization of a dependent noexcept operand.
struct Concrete {};

template <class T>
int measureInterleavedOwner() {
	using MemberPointer = int* (T::*)() const;
	return noexcept(static_cast<MemberPointer>(nullptr) == nullptr) ? 1 : 0;
}

template <class T>
int measureFlatOwner() {
	using MemberPointer = int (T::*)() const;
	return noexcept(static_cast<MemberPointer>(nullptr) == nullptr) ? 1 : 0;
}

template <class T>
int measureDependentPointer() {
	return noexcept(static_cast<T*>(nullptr) == nullptr) ? 1 : 0;
}

template <class T>
int measureDependentMemberObject() {
	return noexcept(static_cast<int T::*>(nullptr) == nullptr) ? 1 : 0;
}

int measureConcreteInterleaved() {
	using MemberPointer = int* (Concrete::*)() const;
	return noexcept(static_cast<MemberPointer>(nullptr) == nullptr) ? 1 : 0;
}

int main() {
	const bool interleaved = measureInterleavedOwner<Concrete>() != 0;
	const bool flat = measureFlatOwner<Concrete>() != 0;
	const bool pointer = measureDependentPointer<Concrete>() != 0;
	const bool member_object = measureDependentMemberObject<Concrete>() != 0;
	const bool concrete = measureConcreteInterleaved() != 0;
	return interleaved && flat && pointer && member_object && concrete ? 42 : 0;
}
