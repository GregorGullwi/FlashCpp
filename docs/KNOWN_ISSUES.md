# Known Issues

## Modular-build-only crash: test_template_partial_spec_ool_ctor_template_same_name_overload_ret0.cpp
`ConstructorDeclarationNode::has_template_parameters()` dereferences a null inner
pointer in `InlineVector::size()` during `materializeMatchingConstructorTemplate`.
Only manifests in the Modular build (runs fine under Sharded/Unity). Root cause
appears to be a default-constructed `ConstructorDeclarationNode` whose `InlineVector`
data pointer is never initialized — the Sharded build's different TU arrangement
happens to zero the memory. See `Parser_Templates_Inst_MemberFunc.cpp:1394`.
