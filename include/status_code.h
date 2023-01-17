// Compile-time errors
STATUS(OK, "OK")
STATUS(FATAL, "A fatal error occurred")
STATUS(EOF_IN_STRING, "Unexpected EOF in string literal")
STATUS(NL_IN_STRING, "Unexpected return in string literal")
STATUS(MALFORMED_TOKEN, "Unrecognized token")
STATUS(UNEXPECTED_TOKEN, "Unexpected token")
STATUS(TOO_MANY_LOCALS, "Functions cannot have more than 255 locals")
STATUS(TOO_MANY_UPVALUES, "Functions cannot capture more than 255 locals")
STATUS(TOO_MANY_PARAMETERS, "Functions cannot have more than 16 parameters")
STATUS(TOO_MANY_ARGUMENTS, "Function calls cannot have more than 16 arguments")
STATUS(TOO_MANY_RETURN_VALUES, "Functions cannot return more than 16 values")
STATUS(TOO_MANY_EXPRESSIONS, "Expected fewer expressions")
STATUS(TOO_MANY_EXPRESSIONS_IN_INITIALIZER,
      "'{}' expressions cannot initialize more than 255 properties")
STATUS(TOO_MANY_EXPRESSIONS_IN_LET, "'let' expressions cannot declare more than 16 locals")
STATUS(REFERENCE_BEFORE_INITIALIZE,
      "Variables cannot be referenced before they are initialized")
STATUS(LOCAL_ALREADY_EXISTS, "A local with this name already exists")
STATUS(EXPRESSION_NOT_ASSIGNABLE, "The expression is not assignable")
STATUS(MISSING_END, "'do' blocks need a corresponding 'end'")
STATUS(MISSING_INITIALIZER, "Variables must be initialized")
STATUS(MISSING_IDENTIFIER, "Identifier could not be resolved")

// Run-time errors
STATUS(NOT_NUMERIC, "Expected a number")
STATUS(NOT_RECORD, "Expected a record")
STATUS(NOT_STRING, "Expected a string")
STATUS(NOT_CALLABLE, "Expected a callable value")
STATUS(WRONG_ARITY, "Could not call a function with the wrong number of arguments")
STATUS(ASSERTION_FAILED, "A runtime assertion failed")
