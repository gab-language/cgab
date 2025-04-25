# TODO List
- Make TUPLES more usable
    - By this I mean, lets have them survive across function calls (without trimming)

EG:
```gab
    (2, 3, self.some_msg)
        # Already works

    (self.some_msg, 2, 3)
        # Expand out the tuple from some_msg,and cons 2 and 3 on.
        # This is solved by having all the LOAD_K and LOAD_LOCAL and LOAD_UPV ops respect tuples.

        # THis code is unchanged - constant loading respects the implicit tuple returned by send.

        # 1 OP_LOAD_LOCAL             
        # 2 OP_SEND
        # 3 OP_LOADK
        # 4 OP_LOADK
        # 5 OP_RETURN

    (self.some_msg, self.another_msg)
        # Cons the tuple from another_msg onto some_msg.
        # THIS COMPILES TO:
        # 1 OP_LOAD_LOCAL
        # 2 OP_SEND
        # 3 OP_TRIM 1
        # 4 OP_LOAD_LOCAL
        # 5 OP_SEND
        # 6 OP_RETURN 1 + have

        # CHANGE TO:
        # 1 OP_LOAD_LOCAL             
        # 2 OP_SEND
        # 3 OP_TUPLE
        # 4 OP_LOAD_LOCAL             
        # 5 OP_SEND
        # 6 OP_CONS
        # 7 OP_RETURN

    # How do tail sends work with this tuple/cons setup?
    # We need to have a frame to return

    (self.some_msg, self.another_msg(a*, b*))
        # Introduce OP_TUPLE, OP_CONS
        # 0  OP_LOAD_LOCAL       self
        # 1  OP_SEND             some_msg               
        # 2  OP_TUPLE                        -- TUPLE is stored here, as we begin building a new tuple
        # 2  OP_LOAD_LOCAL       self        -- Stores to new implicit tuple
        # 3  OP_TUPLE                        -- TUPLE is stored here, as we begin building new tuple.
        # 4  OP_LOAD_LOCAL       a           -- Stores to new implicit tuple
        # 5  OP_SEND             *
        # 6  OP_CONS                         -- As we are now building a tuple, CONS result onto tuple
        # 7  OP_LOAD_LOCAL       b           -- Stores to new implicit tuple
        # 8  OP_SEND             *
        # 9  OP_CONS                         -- Continue constructing tuple
        # 10 OP_SEND             another_msg
        # 11 OP_CONS
        # 12 OP_RETURN

        # 1  OP_LOAD_LOCAL       self
        # 2  OP_SEND             some_msg               
        # 3  OP_TRIM 1
        # 4  OP_LOAD_LOCAL       self
        # 5  OP_LOAD_LOCAL       a
        # 6  OP_SEND             *
        # 7  OP_TRIM 1
        # 8  OP_LOAD_LOCAL       b
        # 9  OP_SEND             *
        # 10 OP_SEND             another_msg
        # 11 OP_RETURN           1 + have

        # This situation is more complex.
        #  Here we have a tuple on the stack after the send to some_msg
        #  but also need to hold one on the stack after send of * to with_tuple_arg
        #  Tuples can arbitrarily nest

        # A tuple is a number on the stack that records how many values *below* it
        # should be considered within the tuple
        # OP_CONS takes two tuples on the top of the stack and combines them into one tuple.

        # OP_SEND takes an IMPLICIT TUPLE.
        # OP_LOAD_* modifies said IMPLICIT TUPLE.

        # Tradeoffs?
        #   - Tuple management is moved entirely to runtime. This simplifies implementation,
        #     But does defer some computation to runtime (ie, slowing down)
        
```
This can maybe be done by sharing space in the slot for FB().
The upper 32 bits can be FB
The lower 32 bits can be the number of extra values *under* where this will return to

Also ideally, optimize away this memmove by having locals be indexed under FB().
But that can be another day.
