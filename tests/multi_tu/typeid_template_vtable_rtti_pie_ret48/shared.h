struct TypeidTemplateBase {
	virtual ~TypeidTemplateBase() = default;
};

template <typename T>
int typeidTemplateTag(T) {
	return static_cast<int>(typeid(T) == typeid(T));
}

int firstTypeidTemplateValue();
