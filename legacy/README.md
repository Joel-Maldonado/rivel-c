# Legacy: the previous Rivel compiler

This directory holds the first Rivel implementation, a compiler that
translated a newline-separated dialect (`fn`, `const`/`mut`, `elif`,
`and`/`or`/`not`, `Int`/`Double`/`Bool`/`String`, `${x}` interpolation) to C
and invoked gcc. Its tests and its language reference (`grammar.md`) are here
too.

It is not built, not tested, and not maintained. It stays only as a
reference for the syntax migration described in `docs/spec.md` section 9.
Once nothing here is needed, delete the directory:

```sh
git rm -r legacy
```
