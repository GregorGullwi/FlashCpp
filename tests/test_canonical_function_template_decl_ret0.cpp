// Free function templates publish signature-aware TemplateDeclIds so distinct
// overloads (value vs pointer) keep separate identities while a single
// non-overloaded template still runs. Mixed widths and a struct exercise the
// published path; declared type parameters stamp onto return and parameter
// specifiers, while function body replay stamping stays deferred.
template <typename T>
T identityValue(T value) {
	return value;
}

template <typename T>
T takeValue(T value) {
	return identityValue(value);
}

template <typename T>
T* takePointer(T* value) {
	return value;
}

template <typename T, typename U>
int mixValues(T left, U right) {
	return static_cast<int>(takeValue(left)) + static_cast<int>(takeValue(right));
}

struct Boxed {
	short tag;
};

int main() {
	Boxed boxed{4};
	char narrow = 1;
	char* narrow_ptr = &narrow;
	return mixValues(3.5, 2.5f) + mixValues(boxed.tag, narrow) +
		static_cast<int>(*takePointer(narrow_ptr)) -
		(3 + 2 + 4 + 1 + 1);
}
