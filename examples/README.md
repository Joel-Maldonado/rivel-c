# Rivel examples

Build the compiler once, then run any example from the repository root:

```sh
make
bin/rivelc run examples/hello_world.rivel
bin/rivelc run examples/structs.rivel
bin/rivelc run examples/command_line.rivel Ada 3
```

Each program stands alone. The small examples need no dependencies, input,
or file writes. `command_line.rivel` prints usage when run without arguments.
The neighboring `.stdout` files show the expected output with no arguments.

| Example | What it demonstrates |
| --- | --- |
| [Hello world](hello_world.rivel) | A minimal entry point and output |
| [Variables](variables.rivel) | `:=` inference, explicit types, mutation, and numeric arithmetic |
| [For loops](for_loops.rivel) | Exclusive and inclusive ranges, list and string iteration, `continue` |
| [While loops](while_loops.rivel) | Countdown, `if` / `else if` / `else`, and `break` |
| [Functions](functions.rivel) | Parameters, return types, assertions, and recursion |
| [Structs](structs.rivel) | Fields, methods, named construction, and shared references |
| [Lists](lists.rivel) | Append, insert, remove, iteration, and independent slices |
| [Strings](strings.rivel) | Interpolation, split/join, replacement, and byte indexing |
| [Optionals](optionals.rivel) | Safe parsing, null checks, narrowing, and `??` defaults |
| [Command line](command_line.rivel) | Arguments, validation, stderr, and exit codes |
| [Tic-tac-toe](tic_tac_toe/README.md) | A complete terminal game with an AI opponent |

Rivel uses `struct` for class-style modeling with fields and methods.
Structs are garbage-collected reference types. There is no `class` keyword
or inheritance. See [the language specification](../docs/spec.md) for the
full semantics.

To keep a compiled native executable:

```sh
bin/rivelc -o /tmp/rivel-hello examples/hello_world.rivel
/tmp/rivel-hello
```

Run `make test-examples` to compare the small programs with their expected
output and run the tic-tac-toe integration checks.
