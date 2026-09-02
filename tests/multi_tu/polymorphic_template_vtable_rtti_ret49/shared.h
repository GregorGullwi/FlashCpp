template <typename T>
struct PolymorphicTemplate {
	virtual ~PolymorphicTemplate() = default;
	inline int scaledTag(T value) const {
		return static_cast<int>(value) + 3;
	}
};

template <typename T>
int polymorphicTemplateIdentityTag(const PolymorphicTemplate<T>& object, T value) {
	return object.scaledTag(value);
}
