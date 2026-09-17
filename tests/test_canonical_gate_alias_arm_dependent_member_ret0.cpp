// The parse_type_specifier `<` gate already treats a known class or variable
// template member as a template-id even when the qualifier is a template
// parameter. Alias templates must participate in that same known-template
// test; otherwise T::Meter<Args> is misread as a comparison and the alias
// template declaration fails to parse.
struct Outer {
	template <typename U>
	using Meter = U;
};

template <typename T>
using Through = T::Meter<int>;

using Direct = Outer::Meter<char>;

int main() {
	// Through must parse at declaration time (the `<` gate). Direct checks
	// ordinary non-dependent alias materialization still yields the
	// substituted member type. Instantiating Through is deferred with the
	// dependent-alias family.
	Direct value{};
	if (sizeof(value) != sizeof(char)) {
		return 1;
	}
	return 0;
}
