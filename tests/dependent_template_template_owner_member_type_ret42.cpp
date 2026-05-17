template<typename T>
struct Identity {
	using type = T;
};

template<template<typename> class TemplateOwner, typename U>
struct UsesDependentMemberType {
	using value_type = typename TemplateOwner<U>::type;
	value_type value;
};

int main() {
	UsesDependentMemberType<Identity, int> x;
	x.value = 42;
	return x.value;
}
