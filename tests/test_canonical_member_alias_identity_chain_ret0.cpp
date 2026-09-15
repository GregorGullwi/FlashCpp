// A member alias template under a published namespace-scope class must resolve
// through published identity when it is spelled with a partial namespace
// suffix of the owner chain. The registration spelling key always carries the
// full namespace prefix (getStructQualifiedNameForRegistration), so the legal
// partial spelling used to miss every alias lookup, fell into class-template
// instantiation, and reported "No primary class template found". Identity
// resolution (owner chain -> class EntityId -> TemplateDeclId -> anchored
// TemplateAliasNode) answers both spellings.
namespace n {
	namespace m {
		struct Gauge {
			template <class T>
			using Meter = T;
		};
	}
}

int main() {
	n::m::Gauge::Meter<int> a = 7;
	m::Gauge::Meter<char> b = 65;
	n::m::Gauge::Meter<long long> c = 10;
	return (a - 7) + (b - 65) + static_cast<int>(c - 10);
}
