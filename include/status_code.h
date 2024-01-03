// Compile-time errors
STATUS(OK, "ok")
STATUS(PANIC, "A fatal error occurred")
STATUS(MALFORMED_STRING, "Unexpected character in string literal")
STATUS(MALFORMED_TOKEN, "Unrecognized token")
STATUS(UNEXPECTED_TOKEN, "Unexpected token")
STATUS(CAPTURED_MUTABLE, "Blocks cannot capture mutable variables")
STATUS(BREAK_OUTSIDE_LOOP, "Cannot break outside a loop")
STATUS(TOO_MANY_VARIABLES_IN_DEF, "Cannot define more than 16 variables")
STATUS(INVALID_IMPLICIT, "Cannot implicitly add parameters after locals")
STATUS(TOO_MANY_LOCALS, "Blocks cannot have more than 255 locals")
STATUS(TOO_MANY_UPVALUES, "Blocks cannot capture more than 255 locals")
STATUS(TOO_MANY_PARAMETERS, "Blocks cannot have more than 16 parameters")
STATUS(TOO_MANY_ARGUMENTS, "Blocks calls cannot have more than 16 arguments")
STATUS(TOO_MANY_RETURN_VALUES, "Blocks cannot return more than 16 values")
STATUS(TOO_MANY_EXPRESSIONS, "Expected fewer expressions")
STATUS(TOO_MANY_EXPRESSIONS_IN_INITIALIZER,
       "Record literals cannot initialize more than 255 properties")
STATUS(REFERENCE_BEFORE_INITIALIZE,
       "Variables cannot be referenced before they are initialized")
STATUS(LOCAL_ALREADY_EXISTS, "A local with this name already exists")
STATUS(EXPRESSION_NOT_ASSIGNABLE,
       "The expression on the left is not assignable")
STATUS(MISSING_END, "Block need a corresponding 'end'")
STATUS(MISSING_INITIALIZER, "Variables must be initialized")
STATUS(MISSING_IDENTIFIER, "Identifier could not be resolved")
STATUS(MISSING_RECEIVER, "A message definition should specify a receiver")

// Run-time errors
STATUS(NOT_NUMBER, "Expected a number")
STATUS(NOT_RECORD, "Expected a record")
STATUS(NOT_STRING, "Expected a string")
STATUS(NOT_MESSAGE, "Expected a message")
STATUS(NOT_CALLABLE, "Expected a callable value")
STATUS(OVERFLOW, "Reached maximum call depth")
STATUS(IMPLEMENTATION_EXISTS, "Implementation already exists")
STATUS(IMPLEMENTATION_MISSING, "Implementation does not exist")
