// Template-body replay resolves these dependent direct member template-ids.
// The two Box primaries share a spelling but must retain their outer primary's
// TemplateDeclId when the replayed declarations are materialized.
template<typename T>
struct ReplayWideOwner {
	template<typename U>
	struct Box {
		T outer;
		U inner;
	};
};

template<typename T>
struct ReplayNarrowOwner {
	template<typename U>
	struct Box {
		U inner;
	};
};

struct ReplayPayload {
	int value;
};

template<typename T>
struct ReplayIdentityUse {
	using Wide = typename ReplayWideOwner<T>::template Box<ReplayPayload>;
	using Narrow = typename ReplayNarrowOwner<T>::template Box<ReplayPayload>;

	static int result() {
		return sizeof(Wide) > sizeof(Narrow) &&
			sizeof(Narrow) == sizeof(ReplayPayload) ? 0 : 1;
	}
};

int main() {
	return ReplayIdentityUse<long long>::result();
}
