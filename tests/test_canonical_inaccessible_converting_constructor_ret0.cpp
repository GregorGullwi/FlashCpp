struct PrivateConvert {
	PrivateConvert(int);
private:
	PrivateConvert(double);
};
struct ProtectedConvert {
	ProtectedConvert(int);
protected:
	ProtectedConvert(double);
};
struct DeletedConvert {
	DeletedConvert(int);
	DeletedConvert(double) = delete;
};
struct PrivateCopy {
	PrivateCopy() {}
private:
	PrivateCopy(const PrivateCopy&);
};
struct ProtectedCopy {
	ProtectedCopy() {}
protected:
	ProtectedCopy(const ProtectedCopy&);
};
static_assert(!__is_constructible(PrivateConvert, double));
static_assert(__is_constructible(PrivateConvert, int));
static_assert(__is_constructible(PrivateConvert, char));
static_assert(!__is_constructible(ProtectedConvert, double));
static_assert(__is_constructible(ProtectedConvert, int));
static_assert(!__is_constructible(DeletedConvert, double));
static_assert(__is_constructible(DeletedConvert, int));
static_assert(!__is_constructible(PrivateCopy, const PrivateCopy&));
static_assert(!__is_constructible(ProtectedCopy, const ProtectedCopy&));
int main() { return 0; }
