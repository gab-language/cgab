// Compile-time errors
STATUS(OK, "ok")
STATUS(NONE, "")
STATUS(TERM, "Fiber terminated")
STATUS(PANIC, "A fatal error occurred")
STATUS(MALFORMED_TOKEN, "Unrecognized token")
STATUS(MALFORMED_STRING, "Malformed string")
STATUS(MALFORMED_RECORD_PAIR, "Malformed pair in record expression")
STATUS(MALFORMED_SEND, "Malformed send expression")
STATUS(UNEXPECTED_TOKEN, "Unexpected token")
STATUS(UNEXPECTED_EOF, "Unexpected end of file")
STATUS(INVALID_REST_VARIABLE, "Invalid rest variable")
STATUS(TOO_MANY_LOCALS, "Blocks cannot have more than 255 locals")
STATUS(TOO_MANY_UPVALUES, "Blocks cannot capture more than 255 locals")
STATUS(TOO_MANY_PARAMETERS, "Blocks cannot have more than 16 parameters")
STATUS(TOO_MANY_ARGUMENTS, "Blocks calls cannot have more than 16 arguments")
STATUS(TOO_MANY_RETURN_VALUES, "Blocks cannot return more than 16 values")
STATUS(TOO_MANY_EXPRESSIONS, "Expected fewer expressions")
STATUS(TOO_MANY_EXPRESSIONS_IN_INITIALIZER,
       "Record literals cannot initialize more than 255 properties")
STATUS(REFERENCE_BEFORE_INITIALIZE,
       "Uninitialized variable")
STATUS(UNBOUND_SYMBOL, "Symbols must be bound before they can be referenced")
STATUS(MALFORMED_ASSIGNMENT,
       "Malformed assignment expression")
STATUS(MISSING_END, "Block need a corresponding 'end'")
STATUS(MISSING_INITIALIZER, "Variables must be initialized")

// Run-time errors
STATUS(TYPE_MISMATCH, "Type mismatch")
STATUS(INVALID_SHIFT_AMOUNT, "Invalid shift amount")
STATUS(OVERFLOW, "Reached maximum call depth")
STATUS(IMPLEMENTATION_EXISTS, "Implementation already exists")
STATUS(IMPLEMENTATION_MISSING, "Implementation does not exist")
