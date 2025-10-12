# TODO List
- Messages can only be defined once, otherwise
developers may recreate mutable state by redefining messages.
This would be very slow to do this way.
- Improve error messaging and destructured binding. 
- Improve libraries
    - Render HTML with our builtin UI
    - HTTP/Routing/Server stuff
    - Database Clients
        - Explore implementing EAVT store like datomic with libdata (apache arrow)
    - Spec
- Potential macro implementation?
```gab
if cond then: do
    ...
end else: do
    ...
end
```
syntactically, this is `if` `<exp>` `then:` `<exp>` `else:` `<exp>`, which flattens to an ast of:
```gab
[
  [
    [ <gab\binary 0x6966> ],
    [ <gab\binary 0x636f6e64> ],
    [ then: ],
    [ [ [ 1 ] ] ],
    [ else: ],
    [ [ [ 2 ] ] ]
  ]
]
```
The macro `if` could just pop expressions off the front of this list as it needs. The macro would:
```gab
if: .defmacro rest => do
  (ok, cond) = rest.pop_front.unwrap

  # expect a then:, followed by a body expression
  (ok, expect_then) = rest.pop_front
  (ok, then_body) = rest.pop_front

  # expect an else:, followed by a body expression
  (ok, maybe_else) = rest.pop_front
  (ok, else_body, rest) = rest.pop_front

  # Generate a message, and define an implementation
  msg = Messages.gen('if')

  # Implement the true/false branches for said message
  msg.defcase: {
    true: AST.Block.make(then_body)
    false: AST.Block.make(else_body)
  }
  
  # Return a re-written AST
  [AST.Send.make(cond, msg), rest*]
end
```
The following would also compile fine, because it results in the same stream of AST nodes.
```gab
if

a

then: b else: c
```
```gab
case v
 is\record: do 'record' end
 is\string: 'string'
 else       'something'
```
The above 'case' macro could work similarly. Consume the `message` and `<exp>` pairs, until you hit an `else` pair.
Then generate a `case` message and implement a simple algorithm to iterate through cases. Very doable.

The nasty thing here is that the builtin messages `=` and `=>` become sort of ugly exceptions. They arent macros, they are builtin messages
with compiler-special-cased behavior.
Is it reasonable to say that:
- Identifier macros are *prefix* notation
- Operator macros are *infix* notation

This is fine because these are the natural/current places where identifiers and operators are found. So the macro-system simply checks for a macro
before emitting a load or a send.

What about `if = 2`?
- if syntax would fail and cause an error to be surfaced
What about `a = 2`?
- = macro would run, as no 'a' macro exists.

