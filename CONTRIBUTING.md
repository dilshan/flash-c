# Contributing to Flash C

Thanks for your interest in Flash C. Bug reports, fixes, documentation and new features are all
welcome. This page explains how to get a change into the project. [Back to the README](README.md)

## Before you start

* **Bugs.** Open an issue with the smallest C program that shows the problem, the exact `fcc`
  command line (or the IDE steps), what you expected and what happened. Include the output of
  `fcc` (the first line shows the version) and your distribution.
* **Features and larger changes.** Open an issue first so the idea can be discussed before you
  spend time on it. The compiler targets C89 on x86-64 Linux only; C++, other platforms and
  other architectures are out of scope. The known gaps are listed in the
  [architecture notes](docs/ARCHITECTURE.md#11-known-gaps).
* **Small fixes** (typos, clear bugs, documentation) can go straight to a pull request.

## Setting up

You need a C compiler (gcc or clang), `make`, Python 3 and a terminal of at least 80×25. The
tests compare against the host `gcc`, and formatting needs `clang-format`.

```
git clone git@github.com:dilshan/flash-c.git
cd flash-c
make -j"$(nproc)"
make test
```

[Architecture notes](docs/ARCHITECTURE.md) describe how the compiler, optimizer, library, IDE and
debugger fit together; read the relevant section before changing that part.

## Workflow

The `main` branch is protected: nothing is pushed to it directly. Every change goes through a pull
request.

1. Fork the repository (or, if you have write access, work in a branch of it) and create a branch
   from an up-to-date `main`:

   ```
   git switch main && git pull
   git switch -c fix/short-description
   ```

   Branch names such as `fix/...`, `feature/...`, `docs/...` and `test/...` keep things readable.
2. Make your change in small, focused commits.
3. Run `make format` and `make test` (see below).
4. Push the branch and open a pull request against `main`. Describe what changed and why, and link
   the issue if there is one.
5. The CI workflow builds and runs the whole test suite on every pull request; it must pass before
   the change is merged. Respond to review comments by pushing more commits to the same branch.
6. Once approved, the change is merged by a maintainer.

Keep a pull request to one topic. A bug fix and an unrelated refactor belong in two pull requests.

## Code style

* C sources (compiler, IDE, run-time library, tools) are formatted with `clang-format` using the
  rules in `.clang-format`: 4-space indent, 100 columns, braces on every block, a blank line
  between definitions. **Run `make format` after editing**, and do not hand-format around it.
* Every source file starts with the header block (File, Purpose, Author, License, Project) used
  by the existing files. New files get the same block with the MIT license line.
* Every function has a comment that says what it does, what its parameters mean and what it
  returns. Document the fields of any structure you add.
* Match the naming and idiom of the surrounding code. Compiler code allocates from the arena in
  `util.c`; do not `malloc` and `free` individual objects there.
* The run-time library (`runtime/lib`) is compiled by Flash C itself, so it may only use what
  Flash C supports (no compound literals, for example). Library-internal helpers are `static`
  functions with globally unique `__prefix` names.
* Do not add dependencies. The project builds with a C compiler and `make`; Python is only used
  for tests and generators.

## Tests

`make test` runs everything; CI runs the same command. Add tests with your change:

| You changed | Add or update |
|---|---|
| code generation, the parser, the library | a program in `tests/cases/` (`name.c`; an optional `name.expected` or `name.in`). It is compiled with Flash C and with the host `gcc`, and the outputs must match |
| diagnostics | `tests/diag/` (the expected text and line numbers) |
| a command-line switch | `tests/options.sh` |
| an optimization | a case in `tests/opt.sh` (output equal, file smaller with the pass on); the fuzzer in `tests/optfuzz.py` runs on every build |
| library build options, linking | `tests/libtest.sh` |
| the IDE, dialogs or the debugger | `tests/ide_smoke.py`, `project_smoke.py` or `debug_smoke.py`, which drive the real `fc` on a pseudo-terminal |
| help pages | the example blocks are compiled and run by `tests/helpexamples.py` |

A fix for a bug should come with a test that fails without the fix.

## Documentation

Update the documentation when behaviour changes:

* a new or changed switch or optimization: [compiler reference](docs/COMPILER.md) and the help
  text in `src/cli/fcc.c`;
* a library function: the headers and module comments, then regenerate with
  `python3 tools/gen_libdoc.py` and `make help` (the [library reference](docs/LIBRARY.md) and
  `help/*.fch` are generated; do not edit them by hand);
* IDE behaviour: the [user guide](docs/USER_GUIDE.md) and the help topics;
* internal design: the [architecture notes](docs/ARCHITECTURE.md).

Screenshots in `docs/screenshots` are generated by the real IDE: run
`python3 tools/screenshots.py` after a build.

## Commit messages

Use a short imperative summary line (about 70 characters), a blank line, then a paragraph on why
the change is needed if it is not obvious:

```
Fix constant folding of unsigned shifts

A shift count above the width of the type was folded with the host's
undefined behaviour. Reduce it the same way the generated code does.
```

If an AI assistant helped write a change, say so with a `Co-Authored-By:` trailer.

## Reference material and licensing

Flash C is under the [MIT License](LICENSE); by contributing you agree that your contribution is
released under it. Write your own code. Do not copy code, headers, library source or help text
from Turbo C or any other proprietary product, or from sources whose license is incompatible with
MIT. Flash C uses Turbo C only as a behavioural reference; see the notice in the
[README](README.md#notice-on-the-reference-product).

## Releases

Maintainers publish releases by pushing a version tag; see [releasing](docs/RELEASING.md). Do not
change version numbers in a pull request, because the version comes from the tag.
