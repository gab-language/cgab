#include "cgab.h"
#include "munit/munit.h"

extern struct gab_triple gab;

static MunitResult test_parse_and_compile(const MunitParameter params[],
                                          void *data) {
  const char *module = "test_valid";
  struct gab_parse_argt valid_args = {
      .source = "1 + 1",
      .name = module,
  };

  union gab_value_pair parsed_valid = gab_parse(gab, valid_args);

  munit_assert_uint64(parsed_valid.status, ==, gab_cvalid);

  struct gab_compile_argt comp_args = {
      .ast = parsed_valid.vresult,
      .bindings = gab_erecord(gab),
      .env = gab_listof(gab, gab_erecord(gab)),
      .mod = gab_string(gab, module),
  };
  union gab_value_pair compiled = gab_compile(gab, comp_args);

  munit_assert_uint64(compiled.status, ==, gab_cvalid);
  munit_assert_uint64(gab_valkind(compiled.vresult), ==, kGAB_PROTOTYPE);

  struct gab_parse_argt invalid_args = {.source = "? bad_syntax := ",
                                        .name = "test_invalid"};

  union gab_value_pair parsed_invalid = gab_parse(gab, invalid_args);
  munit_assert_uint64(parsed_invalid.status, ==, gab_cinvalid);
  munit_assert_uint64(gab_valkind(parsed_invalid.vresult), ==, kGAB_RECORD);

  return MUNIT_OK;
}

struct exec_test {
  struct gab_exec_argt in;
  gab_value result[5];
};

static MunitResult test_exec_source(const MunitParameter params[], void *data) {
  struct exec_test exec_test_cases[] = {
      // COMPILE PASS RUNTIME PASS
      {
          {.source = "42", .name = "exec_test_simple"},
          {gab_cvalid, gab_ok, gab_number(42)},
      },
      // COMPILE PASS RUNTIME FAIL
      {
          // Cannot add strings to numbers
          {.source = "1 + 'hello'", .name = "exec_test_typefail"},
          {gab_cvalid, gab_err},
      },
      {
          // Hello message is not defined
          {.source = "1.hello", .name = "exec_test_sendfail"},
          {gab_cvalid, gab_err},
      },
      // COMPILE FAIL
      {
          // Too many locals
          {
              .source =
                  "(A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, "
                  "A14, A15, A16, A17, A18, A19, A20, A21, A22, A23, A24, A25, "
                  "A26, A27, A28, A29, A30, A31, A32, A33, A34, A35, A36, A37, "
                  "A38, A39, A40, A41, A42, A43, A44, A45, A46, A47, A48, A49, "
                  "A50, A51, A52, A53, A54, A55, A56, A57, A58, A59, A60, A61, "
                  "A62, A63, A64, A65, A66, A67, A68, A69, A70, A71, A72, A73, "
                  "A74, A75, A76, A77, A78, A79, A80, A81, A82, A83, A84, A85, "
                  "A86, A87, A88, A89, A90, A91, A92, A93, A94, A95, A96, A97, "
                  "A98, A99, A100, A101, A102, A103, A104, A105, A106, A107, "
                  "A108, A109, A110, A111, A112, A113, A114, A115, A116, A117, "
                  "A118, A119, A120, A121, A122, A123, A124, A125, A126, A127, "
                  "A128, A129, A130, A131, A132, A133, A134, A135, A136, A137, "
                  "A138, A139, A140, A141, A142, A143, A144, A145, A146, A147, "
                  "A148, A149, A150, A151, A152, A153, A154, A155, A156, A157, "
                  "A158, A159, A160, A161, A162, A163, A164, A165, A166, A167, "
                  "A168, A169, A170, A171, A172, A173, A174, A175, A176, A177, "
                  "A178, A179, A180, A181, A182, A183, A184, A185, A186, A187, "
                  "A188, A189, A190, A191, A192, A193, A194, A195, A196, A197, "
                  "A198, A199, A200, A201, A202, A203, A204, A205, A206, A207, "
                  "A208, A209, A210, A211, A212, A213, A214, A215, A216, A217, "
                  "A218, A219, A220, A221, A222, A223, A224, A225, A226, A227, "
                  "A228, A229, A230, A231, A232, A233, A234, A235, A236, A237, "
                  "A238, A239, A240, A241, A242, A243, A244, A245, A246, A247, "
                  "A248, A249, A250, A251, A252, A253, A254, A255, A256) := "
                  "(2)",
              .name = "exec_test_too_many_locals",
          },
          {gab_cinvalid},
      },
      {
          // Too many arguments
          {
              .source =
                  "(A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, "
                  "A14, A15, A16, A17, A18, A19, A20, A21, A22, A23, A24, A25, "
                  "A26, A27, A28, A29, A30, A31, A32, A33, A34, A35, A36, A37, "
                  "A38, A39, A40, A41, A42, A43, A44, A45, A46, A47, A48, A49, "
                  "A50, A51, A52, A53, A54, A55, A56, A57, A58, A59, A60, A61, "
                  "A62, A63, A64, A65, A66, A67, A68, A69, A70, A71, A72, A73, "
                  "A74, A75, A76, A77, A78, A79, A80, A81, A82, A83, A84, A85, "
                  "A86, A87, A88, A89, A90, A91, A92, A93, A94, A95, A96, A97, "
                  "A98, A99, A100, A101, A102, A103, A104, A105, A106, A107, "
                  "A108, A109, A110, A111, A112, A113, A114, A115, A116, A117, "
                  "A118, A119, A120, A121, A122, A123, A124, A125, A126, A127, "
                  "A128, A129, A130, A131, A132, A133, A134, A135, A136, A137, "
                  "A138, A139, A140, A141, A142, A143, A144, A145, A146, A147, "
                  "A148, A149, A150, A151, A152, A153, A154, A155, A156, A157, "
                  "A158, A159, A160, A161, A162, A163, A164, A165, A166, A167, "
                  "A168, A169, A170, A171, A172, A173, A174, A175, A176, A177, "
                  "A178, A179, A180, A181, A182, A183, A184, A185, A186, A187, "
                  "A188, A189, A190, A191, A192, A193, A194, A195, A196, A197, "
                  "A198, A199, A200, A201, A202, A203, A204, A205, A206, A207, "
                  "A208, A209, A210, A211, A212, A213, A214, A215, A216, A217, "
                  "A218, A219, A220, A221, A222, A223, A224, A225, A226, A227, "
                  "A228, A229, A230, A231, A232, A233, A234, A235, A236, A237, "
                  "A238, A239, A240, A241, A242, A243, A244, A245, A246, A247, "
                  "A248, A249, A250, A251, A252, A253, A254, A255, A256) :: "
                  "(2)",
              .name = "exec_test_too_many_arguments",
          },
          {gab_cinvalid},
      },
      {
          {
              .source = "a = 1; () :: (a*) := 10",
              .name = "exec_test_list_bind_to_upvalue",
          },
          {gab_cinvalid},
      },
      {
          {
              .source = "a = 1; () :: (a**) := 10",
              .name = "exec_test_dict_bind_to_upvalue",
          },
          {gab_cinvalid},
      },
      {
          {
              .source = "a = 1; () :: (a) := 10",
              .name = "exec_test_bind_to_upvalue",
          },
          {gab_cinvalid},
      },
      {
          // No binding for symbol 'x'
          {.source = "x", .name = "exec_test_unbound_symbol"},
          {gab_cinvalid},
      },
      {
          // Invalid binding
          {.source = "{}.a := 2", .name = "exec_test_invalid_binding_shape"},
          {gab_cinvalid},
      },
      {
          // Too many list bindings
          {.source = "(x*, y*) := (1 2)",
           .name = "exec_test_invalid_binding_list"},
          {gab_cinvalid},
      },
      {
          // Too many dict bindings
          {.source = "(x**, y**) := (1 2)",
           .name = "exec_test_invalid_binding_dict"},
          {gab_cinvalid},
      },
      {
          // Dist & list binding
          {.source = "(x**, y*) := (1 2)",
           .name = "exec_test_invalid_binding_list_and_dict"},
          {gab_cinvalid},
      },
      {
          // Naked '\' is an invalid token
          {.source = "\\", .name = "exec_test_invalid_token"},
          {gab_cinvalid},
      },
      {
          // \x is an invalid escape sequence
          {.source = "\'\\x\'", .name = "exec_test_invalid_string_escape"},
          {gab_cinvalid},
      },
      {
          // \u[] is a unicode escape sequence with no codepoint
          {.source = "\'\\u[]\'",
           .name = "exec_test_invalid_string_escape_unicode_incomplete"},
          {gab_cinvalid},
      },
      {
          // \u[abcg] is a unicode escape sequence with an invalid codepoint
          // (it contains a 'g', which is not hex)
          {.source = "\'\\u[abcg]\'",
           .name = "exec_test_invalid_string_escape_unicode_invalid"},
          {gab_cinvalid},
      },
      {
          // Expressions cannot start with a send
          {.source = ".hello", .name = "exec_test_invalid_expression_send"},
          {gab_cinvalid},
      },
      {
          // Expressions cannot start with an operator
          {.source = "+", .name = "exec_test_invalid_expression_op"},
          {gab_cinvalid},
      },
  };

  for (uint64_t i = 0; i < LEN_CARRAY(exec_test_cases); i++) {
    struct exec_test testcase = exec_test_cases[i];

    union gab_value_pair result = gab_exec(gab, testcase.in);
    munit_assert_uint64(result.status, ==, testcase.result[0]);

    if (testcase.result[0] == gab_cvalid)
      for (uint64_t j = 1; j < LEN_CARRAY(testcase.result); j++)
        if (testcase.result[j])
          munit_assert_uint64(result.aresult->data[j - 1], ==,
                              testcase.result[j]);
  }

  return MUNIT_OK;
}

static MunitResult test_run_block(const MunitParameter params[], void *data) {

  struct gab_parse_argt build_args = {
      .source = "x * 2",
      .name = "test_build",
      .argv = (const char *[]){"x"},
      .len = 1,
  };
  union gab_value_pair built = gab_build(gab, build_args);

  munit_assert_uint64(built.status, ==, gab_cvalid);

  gab_value block = built.vresult;

  gab_value args[] = {gab_number(10)};
  struct gab_run_argt run_args = {.main = block, .len = 1, .argv = args};

  union gab_value_pair run_res = gab_run(gab, run_args);
  munit_assert_uint64(run_res.status, ==, gab_cvalid);
  munit_assert_uint64(run_res.aresult->data[0], ==, gab_ok);
  munit_assert_uint64(run_res.aresult->data[1], ==, gab_number(20));

  return MUNIT_OK;
}

static MunitResult test_send_message(const MunitParameter params[],
                                     void *data) {
  gab_value receiver = gab_number(100);
  gab_value message = gab_message(gab, "+");

  struct gab_send_argt send_args = {
      .receiver = receiver,
      .message = message,
      .argv =
          (gab_value[]){
              gab_number(50),
          },
      .len = 1,
  };

  union gab_value_pair send_res = gab_send(gab, send_args);

  munit_assert_uint64(send_res.status, ==, gab_cvalid);
  munit_assert_uint64(send_res.aresult->data[0], ==, gab_ok);
  munit_assert_uint64(send_res.aresult->data[1], ==, gab_number(150));

  gab_value bad_message = gab_message(gab, "unknown_message");
  struct gab_send_argt bad_send_args = {
      .receiver = receiver,
      .message = bad_message,
  };

  union gab_value_pair bad_send_res = gab_send(gab, bad_send_args);
  munit_assert_uint64(bad_send_res.status, ==, gab_cvalid);
  munit_assert_uint64(bad_send_res.aresult->data[0], ==, gab_err);

  return MUNIT_OK;
}

// Map the tests to the munit array
static MunitTest exec_tests[] = {
    {
        "/parse_compile",
        test_parse_and_compile,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/exec_source",
        test_exec_source,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/run_block",
        test_run_block,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/send_message",
        test_send_message,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {},
};

// Global suite configuration
const MunitSuite exec_suite = {
    "/exec", exec_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE,
};
