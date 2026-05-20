# Known Issues

- **Deferred alias materialization: non-type pack forwarding still collapses in some paths**
  - Symptom: `template<int... Vs> using A = Target<Vs...>;` can materialize as if only one non-type argument was forwarded (`sizeof...(Vs)` observed as `1` instead of full pack size) in at least one alias-materialization path.
  - Additional scope: qualified/computed pack-forwarding patterns such as `const Ts...` and `(Vs + 1)...` are not fully expanded in deferred alias materialization paths yet.
  - Impact: wrong runtime/constexpr results for aliases forwarding non-type parameter packs.
  - Status: open.
