// Compile-time errors
STATUS(OK, "ok")
STATUS(NONE, "")
STATUS(TERM, "Fiber terminated")
STATUS(PANIC, "A fatal error occurred")
STATUS(MALFORMED_TOKEN, "Unrecognized token")
STATUS(MALFORMED_STRING, "Malformed string value")
STATUS(MALFORMED_EXPRESSION, "Malformed expression")
STATUS(MALFORMED_ASSIGNMENT, "Malformed assignment expression")
STATUS(UNEXPECTED_EOF, "Unexpected end of file")
STATUS(TOO_MANY_LOCALS, "Blocks cannot have more than 255 locals")
STATUS(TOO_MANY_UPVALUES, "Blocks cannot capture more than 255 locals")
STATUS(UNBOUND_SYMBOL, "Symbols must be bound before they can be referenced")
STATUS(MISSING_INITIALIZER, "Variables must be initialized")

// Run-time errors
STATUS(TYPE_MISMATCH, "Type Mismatch")
STATUS(SPECIALIZATION_MISSING, "Missing Specialization")
STATUS(OVERFLOW, "Fiber stack overflow")
