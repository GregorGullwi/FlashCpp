// Regression: explicit template-id calls inside template bodies must preserve
// their resolved semantic target through instantiation instead of relying on
// mangled-name recovery in sema. A later non-template overload with the same
// name must not steal the call.
template<class T>
int explicitTemplateTarget(T) {
	return 1;
}

template<class T>
int callExplicitTemplateTarget(T value) {
	return explicitTemplateTarget<T>(value) - 1;
}

int explicitTemplateTarget(int) {
	return 100;
}

int main() {
	return callExplicitTemplateTarget(0);
}
