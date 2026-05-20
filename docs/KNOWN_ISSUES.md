# Known Issues

- **Deferred alias materialization: non-type pack forwarding still collapses in some paths**
  - Symptom: `template<int... Vs> using A = Target<Vs...>;` can materialize as if only one non-type argument was forwarded (`sizeof...(Vs)` observed as `1` instead of full pack size) in at least one alias-materialization path.
  - Additional scope: qualified/computed pack-forwarding patterns such as `const Ts...` and `(Vs + 1)...` are not fully expanded in deferred alias materialization paths yet.
  - Impact: wrong runtime/constexpr results for aliases forwarding non-type parameter packs.
  - Status: open.

- **Variable-template initializer replay still drops some dependent member variable-template ids**
  - Symptom: replayed initializers can still collapse `Owner<T>::template value<U>` into a plain qualified-id after replay-local qualifier rewriting, so constexpr lookup later sees only `Owner$inst::value`.
  - Impact: constexpr evaluation of replayed member variable-template references can fail even though the owner instantiation succeeds.
  - Status: open.
