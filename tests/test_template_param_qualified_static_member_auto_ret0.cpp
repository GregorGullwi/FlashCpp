// Regression: expression typing for T::value inside an instantiated template
// member body must resolve T through the current instantiation, so auto
// deduction does not leave a placeholder type for codegen.
struct TemplateParamQualifiedStaticMemberAutoPolicy {
	static constexpr int value = 42;
};

template <class T>
struct TemplateParamQualifiedStaticMemberAutoUse {
	static int get() {
		auto result = T::value;
		return result;
	}
};

int main() {
	return TemplateParamQualifiedStaticMemberAutoUse<TemplateParamQualifiedStaticMemberAutoPolicy>::get() == 42 ? 0 : 1;
}
