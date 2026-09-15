// Two same-spelling member variable templates under distinct published owners
// must stay distinct. Their legacy registry keys are the bare member name and
// the enclosing-name chain ("Inner::Meter"), both of which collide, so
// qualified uses used to resolve through the bare-name fallback to whichever
// template registered last. Identity resolution (owner chain -> class
// EntityId -> TemplateDeclId -> anchored TemplateVariableDeclarationNode)
// answers each owner's own published id.
struct OuterA {
	struct Inner {
		template <class T>
		static constexpr int Meter = 42;
	};
};

struct OuterB {
	struct Inner {
		template <class T>
		static constexpr double Meter = 100.5;
	};
};

int main() {
	constexpr int a = OuterA::Inner::Meter<int>;
	constexpr double b = OuterB::Inner::Meter<char>;
	return (a - 42) + static_cast<int>(b - 100.5);
}
