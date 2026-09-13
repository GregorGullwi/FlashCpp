// A primary member class template directly inside a published class template
// owns a distinct TemplateDeclId. Its declared parameter is therefore a
// canonical TemplateParameter instead of inheriting the enclosing template.
template <typename Outer>
struct TemplateOwner {
	template <typename Inner>
	struct Box {
		Inner value;
	};
};

int main() {
	TemplateOwner<double>::Box<short> box{7};
	return static_cast<int>(box.value) - 7;
}
