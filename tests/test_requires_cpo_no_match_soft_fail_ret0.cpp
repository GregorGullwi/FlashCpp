// Minimal: nested requires calling CPO with no viable operator() must soft-fail, not hard-error.
struct Cpo {
	template <class T>
	static constexpr void operator()(T (&)[2])
		requires requires(Cpo fn, T x) { fn(x); }
	{
		(void)0;
	}
};

int main() {
	(void)sizeof(Cpo);
	return 0;
}
