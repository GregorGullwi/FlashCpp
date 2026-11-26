// [dcl.init.ref]/5.4.3: a conversion result related to the target referent
// through a base class may not drop volatile while the derived-to-base
// adjustment is applied for the indirect binding.
struct BasePart {
	int value;
};

struct DerivedPart : BasePart { };

struct VolatileDerivedSource {
	operator volatile DerivedPart&();
};

const BasePart& r = VolatileDerivedSource();
