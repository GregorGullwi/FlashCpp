template <typename T>
struct strip_cv {
	using type = T;
};

template <typename T>
struct strip_cv<const T> {
	using type = T;
};

template <typename T>
using strip_cv_t = typename strip_cv<T>::type;

template <typename T>
concept Always = true;

template <typename T>
concept CanDefault = requires {
	T{};
};

template <typename T>
class box {
public:
	box() {}

	template <typename U>
		requires CanDefault<U>
	box(const box<U>&) noexcept(CanDefault<U>) {}

private:
	union {
		strip_cv_t<T> value;
	};
};

template <Always T>
	requires CanDefault<T>
class box<T> {
public:
	box() {}

	template <typename U>
		requires CanDefault<U>
	box(const box<U>&) noexcept(CanDefault<U>) {}

private:
	strip_cv_t<T> value{};
};

template <typename T>
struct holder {
	box<T> current;
};

template <typename T>
struct nested_holder {
	box<box<T>> current;
};

using B01 = box<int>;
using B02 = box<B01>;
using B03 = box<B02>;
using B04 = box<B03>;
using B05 = box<B04>;
using B06 = box<B05>;
using B07 = box<B06>;
using B08 = box<B07>;
using B09 = box<B08>;
using B10 = box<B09>;
using B11 = box<B10>;
using B12 = box<B11>;
using B13 = box<B12>;
using B14 = box<B13>;
using B15 = box<B14>;
using B16 = box<B15>;
using B17 = box<B16>;
using B18 = box<B17>;
using B19 = box<B18>;
using B20 = box<B19>;
using B21 = box<B20>;
using B22 = box<B21>;
using B23 = box<B22>;
using B24 = box<B23>;
using B25 = box<B24>;
using B26 = box<B25>;
using B27 = box<B26>;
using B28 = box<B27>;
using B29 = box<B28>;
using B30 = box<B29>;
using B31 = box<B30>;
using B32 = box<B31>;
using B33 = box<B32>;
using B34 = box<B33>;
using B35 = box<B34>;
using B36 = box<B35>;
using B37 = box<B36>;
using B38 = box<B37>;
using B39 = box<B38>;
using B40 = box<B39>;
using B41 = box<B40>;
using B42 = box<B41>;
using B43 = box<B42>;
using B44 = box<B43>;
using B45 = box<B44>;
using B46 = box<B45>;
using B47 = box<B46>;
using B48 = box<B47>;
using B49 = box<B48>;
using B50 = box<B49>;
using B51 = box<B50>;
using B52 = box<B51>;
using B53 = box<B52>;
using B54 = box<B53>;
using B55 = box<B54>;
using B56 = box<B55>;
using B57 = box<B56>;
using B58 = box<B57>;
using B59 = box<B58>;
using B60 = box<B59>;
using B61 = box<B60>;
using B62 = box<B61>;
using B63 = box<B62>;
using B64 = box<B63>;
using B65 = box<B64>;
using B66 = box<B65>;
using B67 = box<B66>;
using B68 = box<B67>;
using B69 = box<B68>;
using B70 = box<B69>;
using B71 = box<B70>;
using B72 = box<B71>;
using B73 = box<B72>;
using B74 = box<B73>;
using B75 = box<B74>;
using B76 = box<B75>;
using B77 = box<B76>;
using B78 = box<B77>;
using B79 = box<B78>;
using B80 = box<B79>;
using B81 = box<B80>;
using B82 = box<B81>;
using B83 = box<B82>;
using B84 = box<B83>;
using B85 = box<B84>;
using B86 = box<B85>;
using B87 = box<B86>;
using B88 = box<B87>;
using B89 = box<B88>;
using B90 = box<B89>;
using B91 = box<B90>;
using B92 = box<B91>;
using B93 = box<B92>;
using B94 = box<B93>;
using B95 = box<B94>;
using B96 = box<B95>;

int main() {
	holder<int> h;
	nested_holder<int> n;
	holder<B96> deep;
	(void) h;
	(void) n;
	(void) deep;
	return 0;
}
