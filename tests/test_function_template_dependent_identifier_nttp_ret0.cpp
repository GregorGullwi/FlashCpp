template<auto Size, auto Alignment>
int checkTemplateArgs();

template<typename T>
struct FunctionTemplateArgUse {
	static constexpr auto required_alignment = alignof(T);
	using FnType = decltype(checkTemplateArgs<sizeof(T), required_alignment>);
};

int main() {
	FunctionTemplateArgUse<long long>::FnType* ptr = nullptr;
	return ptr == nullptr ? 0 : 1;
}
