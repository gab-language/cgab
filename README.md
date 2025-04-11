# Gab
Gab is a dynamic scripting language. It's goals are:
- be *simple* - in design and implementation. 
- be *fast*. Performance is a feature.
- be *embeddable*. The c-api should be stable, simple, and productive.
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

range: 
    .make(0 20000)
    .each spawn_task
```
# TOC
- Language Tour
- Imports
- Modules
- Dependencies
- Installation
### Language Tour
Programs are data. LISPs (such as the aforementioned Clojure) take this literally - code is a *data structure* that can be manipulated by *other code*.
Smalltalk and Self are message-oriented. Code is composed of values and messages. Values receive messages, and behavior emerges. Gab takes inspiration from these two ideas. Here is some example code:
```gab
    'Hello world!' .println
```
This code resembles Smalltalk somewhat, and can be transcribed in English as:
```
    Send the message 'print' to the value 'Hello world!'
```
Here is an alternative, equally valid syntax:
```gab
    println:.('Hello world!')
```
This syntax might look more familiar to programmers in the c-family of languages (Besides the curious '\\'). Said programmers might transcribe this block as:
```
    Call the function 'print' with the argument 'Hello world!'
```
This interpretation isn't wrong, but this is __more__ accurate:
```
    Send the empty message to the value '\print' with the argument 'Hello world!'
```
This might explain the peculiar syntax `\print`. This is actually a *message literal*.
To make an analogy to traditional classes and OOP, think of this as a generic value-representation for a method.
Polymorphism works as you'd expect. \+ behaves differently depending on the receiver:
```gab
    1 + 1                       # => 2
    :+.(1, 1)                   # => 2

    'Hello ' + 'world!'         # => 'Hello world!'
    :+.('Hello ', 'world!')     # => 'Hello world!'
```
To peel back another layer, lets define Gab's syntax a little more clearly:
#### Numbers
Numbers are represented as IEEE 64-bit floats.
```gab
    1
    -4
    5.9
```
#### Strings
Strings are just arrays of bytes. They support some escape sequences.
```gab
    'Hello'
    "world!"
    'Escape unicode: \u[2502]'
```
#### Blocks
Blocks are functions. They always include an implicit variable `self`.
Depending on the context that the block is called in, `self` will have different values.
```gab
    add = (a b) => do
        a + b
    end

    add.(1 2) # => 3
```
#### Records
Records are collections of key-value pairs. They are ordered, and structurally typed.
```gab
    a_record = { work: 'value' }

    a_record                    # => { work = 'value', more_complex_work = <gab.block ...> }

    a_record.work               # => 'value'

    a_tuple = [1 2 3]           # => A record as above, but the keys are ascending integers (starting from 0)

    a_tuple                     # => [1 2 3]
```
Records, like all values, are __immutable__. This means that setting values in records returns a *new record*.
```gab
    a_record = { work: 'value' }

    a_record = a_record .work 'another value'   # => When an argument is provided, this message serves as a 'set' instead of a 'get'.

    a_record .println                           # => { work = 'another value' }
```
Normally this would be incredibly expensive, copying entire datastructures just to make a single mutation.
Gab's records are implemented using Bit-Partitioned Persistent Vectors, which use structural sharing to reduce memory impact and reduce the cost of copying.
#### Messages as values
Messages are similar to strings (and interchangeable in some ways). However, they respond to messages differently.
```gab
    hello:                  # => hello: (as a message)

    'hello'                 # => hello (as a string)
    
    hello: == 'hello'       # => false

    # ?: is the message for getting the type of a value
    hello: ?                # => hello:

    'hello' ?               # => gab\string
```
Because message's have a different type, you can define how an *individual message* responds to a message. Here is an example:
```gab
# Define the message 'then' for the .true and .false message.
then: .defcase {
    true: do callback
        # In the true path, we call the callback
        callback.()
    end
    false: do
        # In the false path, we return false
        self
    end
}
```
#### Messages as behavior
Messages are the *only* mechanism for defining behavior or control flow in gab.
```gab
    print:                  # => print:

    1 .print                # => prints 1

    print:.(1 2 3)          # => prints 1, 2, 3
```
The core library provides some messages for defining messages. This might feel a little lispy:
```gab
say_hello: .def('gab\string', 'gab\message', () => 'Hello, ' + self.to\s.println)

'Joe' .say_hello    # => Hello, Joe!

Rich: .say_hello    # => Hello, Rich!

meet: .defcase {
    Joe:  () => 'Nice to meet you Joe!' .println
    Rich: () => 'Its a pleasure, Rich!' .println
}

Joe:.meet          # => Nice to meet you Joe!

Rich:.meet         # => Its a pleasure, Rich!
```
### Behavior
Behavior in Gab is dictated *exclusively* by polymorphic, infix messages. These infix messages always have one left-hand value and up to one right-hand value.
However, the tuple syntax (eg: `(3, 4)`) allows multiple values to be wrapped into one. Blocks can return multiple values in the same way.
```gab
    # Send \+ to  1 with an argument of 1
    1 + 1

    # Send \do_work to val with an argument of (2, 3, 4)
    val .do_work(2 3 4)

    (ok result) = val .might_fail 'something'
```
### Concurrency
New programming languages that don't consider concurrency are boring! Everyones walking around with 8+ cores on them at all times, might as well use em!
Gab's runtime uses something similar to goroutines or processes. A `gab\fiber` is a lightweight thread of execution, which are quick to create/destroy.
For communication between fibers, Gab provides the `gab\channel`. Currently, these are unbuffered channels. This means that both `put` and `take` operations
are always blocking.
- When putting to a channel, the putting fiber must block until a receiving fiber is available on the other end.
- The same applies when taking from a channel.

Unbuffered channels are especially unique because _they never own a value_. They are cheaper to manage with garbage collection as a result!
Gab's scheduler/runtime is actually implemented using an unbuffered _channel of fibers_.
When a user creates a fiber (like with `gab\fiber () => do ... end`), the runtime does something like this:
```js
    const fiber = new Fiber(block_to_run) // Create a new fiber, which is going to run the block
    global_work_channel.blockingPut(fiber)       // Blocking put the new fiber into the work channel. This will block until another thread is available to take it.
```
And the worker threads look something like this:
```js
    while (true) {
        const fiber_to_run = global_work_channel.blockingTake()
        fiber_to_run.execute()
    }
```
This implementation has some problems, but it works well enough as an initial prototype.
```gab
# Define a message for doing some work. This builds a list in a silly way.
do_acc: .defcase {
    true: (acc) => acc
    false: (acc n) => [n acc.acc(n - 1)**]
}

acc: .def(
    'gab\\Record'
# Note the choice to use the send syntax ':==' as opposed to the operator syntax '=='.
# The send syntax has _higher precedence_ than operators. Here is the expression annotated with parentheses
   ((n :== 0) .do_acc(self n)) vs (n == (0 .do_acc(self n)))
    n => n :== 0 .do_acc(self n))

# Define a message for launching blocks in a fiber.
# This just calls the gab\fiber constructer with
#   a single argument - the block
go: .def(
    'gab.block'
    () => Fiber.make self)

# Define some input and output channels
(in out) = (Channels.make, Channels.make)

# Here is our block that will do the work.
# Send \acc to the empty list [] with an
# argument we pull from the in channel. Then
# take the length of that result list, and put it
# on the out channel.
work = () => do
    n = in >!
    res = [] .acc n .len
    out <! res
end

# Dispatch some routines to work
work .go
work .go
work .go

# Dispatch a routine to add work on the in channel
fib = () => do
  in <! 400
  in <! 500
  in <! 600
end .go

fib.println                       # => <gab\fiber 0x...>

# Commas are treated as newlines, which serve _some_ syntactic purpose.
# In this case, they end the expression so that `out >!` doesn't take the argument on the rhs.
(out >!, out >!, out >!) .print # => 400, 500, 600
```
A neat feature built on top of the concurrency model is *concurrent* garbage collection. There is a secret extra thread dedicated to garbage collection, which when triggered
briefly interrupts each running thread to track its live objects. After the brief interruption, each running thread is returned to its work and the collecting thread finishes the collection.
Because the threads all share a single collector, and all gab values are immutable, this means that gab can implement message sends *without any serialization or copying*.
Other models, such as in both Go and Erlang, copy/serialize messages into/out of channels and mailboxes. Gab's model doesn't have this restriction - we can pass values between fibers freely.
Gab uses CSP as its model for communicating between fibers. This is the `go` or `clojure.core.async` model, using channels and operations like `put`, `take`, and `alt/select`.
Gab has an initial implementation of this, and actually uses a `gab\channel` of `gab\fibers` at the runtime level to queue fibers for running.
#### TODO:
- [ ] Instead of malloc/free, write a custom per-job allocator. This can function like a bump allocator, and can enable further optimizations:
    - allocate-as-dead. Objects that *survive* their first collection are *moved* out of the bump allocator, and into long term storage.
        This can work because we are __guaranteed__ to visit all the roots that kept this object alive in that first collection.
    - Moving objects is not safe (we can't traverse all pointers in c-code). The allocate-as-dead strategy is still useful,
        but will need to adjust how locking works (which right now, delays drefing objects until "unlock" is called)
    - Interestingly, it is known at object-creation time whether it is movable or not. Maybe this can be used to choose a specific allocation strategy.
- [ ] Implement buffered channels. (come up with workaround)
    - Channels actually *can't* be buffered because then they'd be mutable. There must always be a putter blocking on the channel until there is a taker.
- [X] Fix memory leak of fibers
    - Currently we intenionally leak fiber objects, this was just a temporary hack
- [X] Refine module system and some ergonomic things for defining messages
    - Fibers store messages independantly of each other. \use runs source files in a separate fiber, so then has to replace
    the calling fibers messages at the end of the call
- [ ] Implement *yielding/descheduling*. When a fiber blocks (like on a channel put/take), that fiber should *yield* to the back of some queue, so that the OS thread can continue work on another fiber.
    - Because Gab doesn't do this currenty, gab code actually can't run on just one thread. Try `gab run -j 1 test`. It will just hang.
    - This can't be implemented without changing the main work-channel to be buffered. 
    - This requires finding a sound strategy for handling thread-migration in the gc.
- [X] Lazily create up to `njobs` threads, instead of immediately creating all `njobs`.
    - Scale idle threads back down after some timeout
    - Be smarter about creating these - check
- [ ] Because of self-modifying bytecode and inline caches. gab copies each compiled module for each OS thread.
    - For blocks of some small-enough heuristic, it might be beneficial to give them a copy of their prototypes bytecode.
    - This would allow each instance of the block to specialize according to the data types it sees as an individual
- [X] Change records to use a datastructure similar to clojure's persistent vectors. 
    - Shapes mean we can still cache lookup indices.
- [X] Allocate records in their final state ahead of time, instead of creating n - 1 intermediate records.
- [ ] Optimize shape datastructure.
    - Shapes are mutable because of their ugly transition vector
    - Building big shapes (like for tuples) is basically traversing a linked list in O(n) time (ugly)
- [ ] Optimize string interning datastructure.
    - Hashmap works well enough, but copies a lot of data and makes concat/slice slow.
- [X] Refactor *OP_TUPLE* and *OP_RECORD* to be message sends. Include as builtins:
    - `.gab.record(<args>)`
    - `.gab.list(<args>)`
    - `.gab.fiber(block)`
    - `.gab.channel()`
    - `\records.pack` -> Pack a tuple of values into a record
    - `\lists.pack`   -> Pack a tuple of values into a list
- [ ] QoL improvements to repl
    - Multiline Editing
    - History
- [ ] JIT Compilation (need I say more)
    - Copy-and-patch JIT compiler? Refactoring VM.c via macros to write into stencils
- [ ] Compose channels with `|`, as opposed to `alt`.
- [ ] Capture sig interrupt to gracefully print stacktraces of running fibers
- [ ] Of course, lots of library work can be done.
    - More iterators
    - Improve spec
    - Wrappers for c-libraries for data parsing, http, sockets, number stuff
# What about imports?
Gab defines several native messages. `:print` is one you should be familiar with by now - `:use` is another!
It is used like this:
```
'io' :use
```
The implementation searches for the following, in order:
 - `./(name).gab`
 - `./(name)/mod.gab`
 - `./lib(name).so`
 - `~/gab/(name)/.gab`
 - `~/gab/(name)/.gab`
 - `~/gab/(name)/mod.gab`
 - `~/gab/std/(name)/.gab`
 - `~/gab/lib/libcgab(name).so`
 Files ending in the `.gab` extension are evaluated, and the result of the last top-level expression is returned to the caller of `:use`. Files ending in the `.so` extension are opened dynamically, and searched for the symbol `gab_lib`. The result of this function is returned to the caller.
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
