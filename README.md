# Gab
Gab is a dynamic scripting language. It's goals are:
- be *simple* - in design and implementation. 
- be *fast*. Performance is a feature.
- be *embeddable*. The c-api should be stable, simple, and productive.
For in-depth documentation and exploration, see [gab's website](https://gab-language.github.io/site/).
## Inspiration
Gab is heavily inspired by [Clojure](https://clojure.org), [Self](https://selflanguage.org/), [Lua](https://www.lua.org/), and [Erlang](https://www.erlang.org/).
```gab
spawn_task = (i) => do
    Fibers.make () => do
        Strings
            .make('Hello from ', i)
            .println
    end
end

Ranges
    .make(0 20000)
    .each spawn_task
```
### Project Structure
There are several sub-projects within this repository.
- The first is `cgab`, a static c-library which provides the functionality of the Gab language itself. The source files for this library are in `src/cgab`.
- The second is `gab`, the cli tool which *depends on* `cgab`, and provides an interface to the programmer for running and compiling Gab code. The source files are found in `src/gab`.
- The third is a collection of builtin c-modules, the source files for which are found in `src/mod`. Each of these files is a unique Gab c-module, and are compiled independantly from one another.
- The fourth is a collection of builtin Gab modules, the source files for which are found in `mod/`. These files are installed alongside the dynamic c-modules above when installing a version of Gab with `gab get`
- There are two other Gab modules `repl` and `lsp`, which are likely to be broken out into their own repos in the future.
#### TODO:
- [ ] Instead of malloc/free, write a custom per-job allocator. This can function like a bump allocator, and can enable further optimizations:
    - allocate-as-dead. Objects that *survive* their first collection are *moved* out of the bump allocator, and into long term storage.
        This can work because we are __guaranteed__ to visit all the roots that kept this object alive in that first collection.
    - Moving objects is not safe (we can't traverse all pointers in c-code). The allocate-as-dead strategy is still useful,
        but will need to adjust how locking works (which right now, delays drefing objects until "unlock" is called)
    - Interestingly, it is known at object-creation time whether it is movable or not. Maybe this can be used to choose a specific allocation strategy.
- [ ] Optimize shape datastructure.
    - Shapes are mutable because of their ugly transition vector
    - Building big shapes (like for tuples) is basically traversing a linked list in O(n) time (ugly)
    - Include some optimizations for records, like popping values, swapping shapes with the same value, etc.
        - A *pop_front* operation can point to the same underlying record, just with a different shape! (And some sort of tombstone thing) 
- [ ] Optimize string interning datastructure.
    - Hashmap works well enough, but copies a lot of data and makes concat/slice slow.
- [ ] JIT Compilation (need I say more)
    - Copy-and-patch JIT compiler? Refactoring VM.c via macros to write into stencils
# Dependencies
libc is the only dependency.
# Installation
This project is built with `Make`. The `Makefile` expects some environment variables to be present. Specifically:
  - `GAB_PREFIX`: A place for gab to install data without root. This is for packages and other kinds of things. Something like `/home/<user>`
  - `GAB_INSTALLPREFIX`: Where gab should install itself - the executable, header files, libcgab, etc. Something like `/usr/local`
  - `GAB_CCFLAGS`: Additional flags ot pass to the c compiler.
[Clide](https://github.com/TeddyRandby/clide) is a tool for managing shell scripts within projects. cgab uses it to manage useful scripts and build configuration. To configure and install with clide, run `clide install` and complete the prompts.
If clide is unavailable, it is simple to just use make. To install a release build:
  1. `GAB_PREFIX="<path>" GAB_INSTALL_PREFIX="<path>" GAB_CCFLAGS="-O3" make build`
  2. `sudo make install`
# C-API Documentation
The c-api is contained in the single header file `gab.h`. You can generate documentation with `clide docs`, or by just running `doxygen`.
