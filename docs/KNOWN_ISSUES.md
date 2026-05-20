# Known Issues

- **Variable-template initializer replay still drops some dependent member variable-template ids**
  - Symptom: replayed initializers can still collapse `Owner<T>::template value<U>` into a plain qualified-id after replay-local qualifier rewriting, so constexpr lookup later sees only `Owner$inst::value`.
  - Impact: constexpr evaluation of replayed member variable-template references can fail even though the owner instantiation succeeds.
  - Status: open.
