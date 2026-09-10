# Gab
Gab is a dynamic scripting language. It's goals are:
- be *simple* - in design and implementation. 
- be *fast*. Performance is a feature.
- be *embeddable*. The c-api should be stable, simple, and productive.

For in-depth documentation and exploration, see [gab's website](https://gab-language.github.io/site/).

# Inspiration
Gab is heavily inspired by [Clojure](https://clojure.org), [Self](https://selflanguage.org/), [Lua](https://www.lua.org/), and [Erlang](https://www.erlang.org/).
```gab
spawn_task := (i) :: do
    Fibers.make () :: do
        Strings
            .make('Hello from ', i)
            .println
    end
end

Ranges
    .make(0 20000)
    .each spawn_task
```

If you're looking for an example of what a gab project looks like, check out [this demo](https;//github.com/gab-language/gwordle).

# Development

## Project Structure
There are several sub-projects within this repository.
- The first is `cgab`, a c-library which provides the functionality of the Gab language itself. The source files for this library are in `src/cgab`.
- The second is `gab`, the cli tool which *depends on* `cgab`, and provides an interface to the programmer for running and compiling Gab code. The source files are found in `src/gab`.
- The third is a collection of builtin c-modules, the source files for which are found in `src/mod`. Each of these files is a unique Gab c-module, and are compiled independantly from one another.
- The fourth is a collection of builtin Gab modules, the source files for which are found in `github.com/gab-language/cgab@<version>/mod/`. These files are installed alongside the dynamic c-modules above when installing a version of Gab with `gab get`

Furthermore, this project uses `clide` to organize tooling and useful development scripts. This is not necessary for development, but it does make it easier.

## Dependencies
libc is the only dependency for the cgab static library itself. It does require the C11 threads api, however.
There are many native modules which do require dependencies. All of these are vendored in the `vendor` directory as git submodules.
Therefore, all that is needed to clone `cgab` and all its dependencies for building `gab`, `cgab`, and all standard-library modules is `git clone --recurse-submodules`

## Building from source
This project is built with `Make` - and thus development of cgab is only supported on unix systems (WSL untested).

After cloning recursively, running `make` should suffice to build everything for your system.
For cross compiling, `clide` is used to write configuration scripts for as many targets as you like, and build them all.

## C-API Documentation
The c-api is contained in the single header file `cgab.h`. You can generate documentation with `clide docs`, or by just running `doxygen`.

# Contributing

Gab is fully free and open source, but is governed by a benevolent-dicator-for-life. I (Teddy Randby) have final say over gab's implementation and design.

## Use gab
The best way to contribute to gab right not is to *use* it and provide feedback and bug reports. This will make gab better for everyone!

## Talk about gab
Secondly, anything you can do to spread the word and get people excited about gab will go a long way.

## AI Policy
gab is strictly no-llm and no-ai. While this policy is born out of my (Teddy Randby) opinions, it is also a concious choice to create a policy like [zig's](https://codeberg.org/ziglang/zig).

To clarify:
- No llms for issues
- No llms for pull requests
- No llms in comments/replies
