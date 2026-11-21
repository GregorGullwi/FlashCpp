// [stmt.ambig]: T(a)[4] is a declaration of an array of 4 T.
// Perturbation: calling requireCanonicalShape from the declaration/expression lookahead
// (while SpeculativeParseRegion is active) throws and rejects this translation unit.
// Parsing the declarator as four pointers makes sizeof(a) / sizeof(T) return 8.
using T = int;

int main() {
	T (a)[4];
	return static_cast<int>(sizeof(a) / sizeof(T));
}
