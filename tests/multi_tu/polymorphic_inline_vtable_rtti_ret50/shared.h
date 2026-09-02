struct PolymorphicBase {
	virtual ~PolymorphicBase() = default;
	inline int tagValue() const {
		return 7;
	}
};

int polymorphicIdentityTag(const PolymorphicBase& object);
