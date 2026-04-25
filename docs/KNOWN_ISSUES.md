# Known Issues

## Dependent member-type access used as a non-type template argument value

A class template that inherits from a base whose non-type template argument
is itself a `Trait<dependent>::value` style member access — for example:

```cpp
template <typename T>
struct is_integral
    : integral_constant<bool, __is_integral_helper<typename remove_cv<T>::type>::value> {};
```

…is not yet fully supported. During instantiation, the inner placeholder
(`__is_integral_helper$<dep>`) is re-instantiated, but multi-step struct-template
chains used to compute its dependent type argument (e.g.,
`remove_cv<T>::type` → `remove_const<remove_volatile<T>::type>::type`) are not
recursively materialized, so the inner trait does not resolve to its exact
specialization. The standard `<type_traits>` shape, where the trait inherits
from `Helper<__remove_cv_t<T>>::type`, *is* supported because `__remove_cv_t`
is a transparent alias template and the deferred-base resolver can re-bind
member-type aliases against the concrete instantiation.

A regression test for this scenario is intentionally not present; if you need
to revisit this, the original dispatch-based test in this PR's history (using
hand-rolled `remove_cv` struct chain) is a good starting point.
