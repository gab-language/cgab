#include "arrow/api.h"
#include "arrow/compute/api.h"
#include "arrow/csv/api.h"
#include "arrow/io/api.h"

#include "gab.h"
#include "platform.h"

/*
 * A custom input stream class which yields to the gab engine
 * every time it reads.
 *
 * It is required by Gab's capi that user code in c/c++ modules
 * not block for an unknown amount of time without yielding.
 */
class GabInputStream : public arrow::io::InputStream {
public:
  struct gab_triple gab;
  std::shared_ptr<arrow::io::ReadableFile> rf;

  GabInputStream(struct gab_triple gab,
                 std::shared_ptr<arrow::io::ReadableFile> rf)
      : gab(gab), rf(rf) {};

  static arrow::Result<std::shared_ptr<GabInputStream>>
  Open(struct gab_triple gab, std::string path) {
    auto rf = arrow::io::ReadableFile::Open(path);
    if (!rf.ok())
      return rf.status();

    return arrow::Result(
        std::make_shared<GabInputStream>(gab, rf.MoveValueUnsafe()));
  };

  arrow::Status Close() override { return rf->Close(); };
  arrow::Result<int64_t> Tell() const override { return rf->Tell(); };
  bool closed() const override { return rf->closed(); };

  arrow::Result<int64_t> Read(int64_t nbytes, void *out) override {
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return arrow::Status::IOError("Operation Canceled");
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    };

    return rf->Read(nbytes, out);
  };

  arrow::Result<std::shared_ptr<arrow::Buffer>> Read(int64_t nbytes) override {
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return arrow::Status::IOError("Operation Canceled");
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    };

    return rf->Read(nbytes);
  };
};

#define GAB_DATAMOD "gab\\data"
#define GAB_ARRAY "gab\\array"
#define GAB_TENSOR "gab\\tensor"
#define GAB_CHUNKED_ARRAY "gab\\array\\chunked"
#define GAB_SCHEMA "gab\\schema"
#define GAB_TABLE "gab\\table"

template <typename T>
gab_value wrap(struct gab_triple gab, const char *tname,
               std::shared_ptr<T> ptr) {
  std::shared_ptr<T> *allocated = new std::shared_ptr(ptr);
  return gab_box(gab, (struct gab_box_argt){
                          .size = sizeof(std::shared_ptr<T> *),
                          .data = allocated,
                          .type = gab_string(gab, tname),
                          .destructor =
                              [](gab_triple, uint64_t, char *p) {
                                auto sp = static_cast<std::shared_ptr<T> *>(
                                    (void *)p);
                                sp->~shared_ptr();
                              },
                      });
}

template <typename T> std::shared_ptr<T> unwrap(gab_value box) {
  return *static_cast<std::shared_ptr<T> *>(gab_boxdata(box));
}

#define GAB_DATATYPE_INT8 "i8"
#define GAB_DATATYPE_INT16 "i16"
#define GAB_DATATYPE_INT32 "i32"
#define GAB_DATATYPE_INT64 "i64"
#define GAB_DATATYPE_UINT8 "u8"
#define GAB_DATATYPE_UINT16 "u16"
#define GAB_DATATYPE_UINT32 "u32"
#define GAB_DATATYPE_UINT64 "u64"
#define GAB_DATATYPE_STRING tGAB_STRING
#define GAB_DATATYPE_BINARY tGAB_BINARY
#define GAB_DATATYPE_NUMBER tGAB_NUMBER

std::optional<const std::shared_ptr<arrow::DataType>>
valToDatatype(struct gab_triple gab, gab_value m) {
  if (m == gab_message(gab, GAB_DATATYPE_INT8))
    return arrow::int8();
  if (m == gab_message(gab, GAB_DATATYPE_INT16))
    return arrow::int16();
  if (m == gab_message(gab, GAB_DATATYPE_INT32))
    return arrow::int32();
  if (m == gab_message(gab, GAB_DATATYPE_INT64))
    return arrow::int64();
  if (m == gab_message(gab, GAB_DATATYPE_UINT8))
    return arrow::uint8();
  if (m == gab_message(gab, GAB_DATATYPE_UINT16))
    return arrow::uint16();
  if (m == gab_message(gab, GAB_DATATYPE_UINT32))
    return arrow::uint32();
  if (m == gab_message(gab, GAB_DATATYPE_UINT64))
    return arrow::uint64();
  if (m == gab_message(gab, GAB_DATATYPE_NUMBER))
    return arrow::float64();
  if (m == gab_message(gab, GAB_DATATYPE_BINARY))
    return arrow::binary();
  if (m == gab_message(gab, GAB_DATATYPE_STRING))
    return arrow::utf8();

  return std::nullopt;
}

std::optional<arrow::Datum> valToDatum(struct gab_triple gab, gab_value value) {
  switch (gab_valkind(value)) {
  case kGAB_NUMBER:
    return arrow::Datum(gab_valtof(value));
  case kGAB_BOX: {
    if (gab_boxtype(value) == gab_string(gab, GAB_ARRAY))
      return unwrap<arrow::Array>(value);

    if (gab_boxtype(value) == gab_string(gab, GAB_TABLE))
      return unwrap<arrow::Table>(value);
  }
  default:
    assert(false && "UNHANDlED DATUM");
    return std::nullopt;
  }
}

gab_value datumToValue(struct gab_triple gab, arrow::Datum datum) {
  switch (datum.kind()) {
  case arrow::Datum::NONE:
    return gab_nil;
  case arrow::Datum::ARRAY:
    return wrap(gab, GAB_ARRAY, arrow::MakeArray(datum.array()));
  case arrow::Datum::CHUNKED_ARRAY:
    return wrap(gab, GAB_CHUNKED_ARRAY, datum.chunked_array());
  case arrow::Datum::TABLE:
    return wrap(gab, GAB_TABLE, datum.table());
  case arrow::Datum::RECORD_BATCH:
    assert(false && "Unhandled datum");
    break;
  case arrow::Datum::SCALAR:
    switch (datum.type()->id()) {
    case arrow::Type::BOOL:
      return gab_bool(datum.scalar_as<arrow::BooleanScalar>().value);
    case arrow::Type::UINT8:
      return gab_number(datum.scalar_as<arrow::UInt8Scalar>().value);
    case arrow::Type::INT8:
      return gab_number(datum.scalar_as<arrow::Int8Scalar>().value);
    case arrow::Type::UINT16:
      return gab_number(datum.scalar_as<arrow::UInt16Scalar>().value);
    case arrow::Type::INT16:
      return gab_number(datum.scalar_as<arrow::Int16Scalar>().value);
    case arrow::Type::UINT32:
      return gab_number(datum.scalar_as<arrow::UInt32Scalar>().value);
    case arrow::Type::INT32:
      return gab_number(datum.scalar_as<arrow::Int32Scalar>().value);
    case arrow::Type::UINT64:
      return gab_number(datum.scalar_as<arrow::UInt64Scalar>().value);
    case arrow::Type::INT64:
      return gab_number(datum.scalar_as<arrow::Int64Scalar>().value);
    case arrow::Type::HALF_FLOAT:
      return gab_number(datum.scalar_as<arrow::HalfFloatScalar>().value);
    case arrow::Type::FLOAT:
      return gab_number(datum.scalar_as<arrow::FloatScalar>().value);
    case arrow::Type::DOUBLE:
      return gab_number(datum.scalar_as<arrow::DoubleScalar>().value);
    case arrow::Type::STRING: {
      auto str = datum.scalar_as<arrow::StringScalar>().value;
      return gab_nstring(gab, str->size(), str->data_as<char>());
    }
    case arrow::Type::BINARY: {
      auto bin = datum.scalar_as<arrow::BinaryScalar>().value;
      return gab_nstring(gab, bin->size(), bin->data_as<char>());
    }
    case arrow::Type::STRING_VIEW: {
      auto str = datum.scalar_as<arrow::StringViewScalar>().value;
      return gab_nstring(gab, str->size(), str->data_as<char>());
    }
    case arrow::Type::BINARY_VIEW: {
      auto bin = datum.scalar_as<arrow::BinaryViewScalar>().value;
      return gab_nstring(gab, bin->size(), bin->data_as<char>());
    }
    case arrow::Type::DECIMAL128:
    case arrow::Type::DECIMAL256:
    case arrow::Type::LIST:
    case arrow::Type::STRUCT:
    case arrow::Type::SPARSE_UNION:
    case arrow::Type::DENSE_UNION:
    case arrow::Type::DICTIONARY:
    case arrow::Type::MAP:
    case arrow::Type::EXTENSION:
    case arrow::Type::FIXED_SIZE_LIST:
    case arrow::Type::DURATION:
    case arrow::Type::LARGE_STRING:
    case arrow::Type::LARGE_BINARY:
    case arrow::Type::LARGE_LIST:
    case arrow::Type::INTERVAL_MONTH_DAY_NANO:
    case arrow::Type::RUN_END_ENCODED:
    case arrow::Type::LIST_VIEW:
    case arrow::Type::LARGE_LIST_VIEW:
    case arrow::Type::DECIMAL32:
    case arrow::Type::DECIMAL64:
    case arrow::Type::MAX_ID:
    case arrow::Type::NA:
    case arrow::Type::FIXED_SIZE_BINARY:
    case arrow::Type::DATE32:
    case arrow::Type::DATE64:
    case arrow::Type::TIMESTAMP:
    case arrow::Type::TIME32:
    case arrow::Type::TIME64:
    case arrow::Type::INTERVAL_MONTHS:
    case arrow::Type::INTERVAL_DAY_TIME:
      assert(false && "Unhandled type");
      return gab_nil;
    }
  }
}

// The following macros define an implementation
// of an array operation in apache arrow.
// They are used in the xmacros below to automatically
// implement each operatin.

#define GAB_ARRAYLIB_UNARY(_, f)                                               \
  GAB_DYNLIB_NATIVE_FN(array, f) {                                             \
    gab_value varr = gab_arg(0);                                               \
    gab_value array_t = gab_string(gab, GAB_ARRAY);                            \
                                                                               \
    if (gab_valtype(gab, varr) != array_t)                                     \
      return gab_ptypemismatch(gab, varr, array_t);                            \
                                                                               \
    auto arr = unwrap<arrow::Array>(varr);                                     \
                                                                               \
    auto res = arrow::compute::f(arr);                                         \
                                                                               \
    if (!res.ok())                                                             \
      return gab_panicf(gab, "$",                                              \
                        gab_string(gab, res.status().ToString().c_str()));     \
                                                                               \
    return gab_vmpush(gab_thisvm(gab),                                         \
                      datumToValue(gab, res.MoveValueUnsafe())),               \
           gab_union_cvalid(gab_nil);                                          \
  }

#define GAB_ARRAYLIB_BINARY(_, f)                                              \
  GAB_DYNLIB_NATIVE_FN(array, f) {                                             \
    gab_value varr = gab_arg(0);                                               \
    gab_value vrhs = gab_arg(1);                                               \
    gab_value array_t = gab_string(gab, GAB_ARRAY);                            \
                                                                               \
    if (gab_valtype(gab, varr) != array_t)                                     \
      return gab_ptypemismatch(gab, varr, array_t);                            \
                                                                               \
    auto arr = unwrap<arrow::Array>(varr);                                     \
    auto rhs = valToDatum(gab, vrhs);                                          \
    if (!rhs.has_value())                                                      \
      return gab_panicf(gab, "Inalid rhs $", vrhs);                            \
    auto res = arrow::compute::f(arr, rhs.value());                            \
                                                                               \
    if (!res.ok())                                                             \
      return gab_panicf(gab, "$",                                              \
                        gab_string(gab, res.status().ToString().c_str()));     \
                                                                               \
    return gab_vmpush(gab_thisvm(gab),                                         \
                      datumToValue(gab, res.MoveValueUnsafe())),               \
           gab_union_cvalid(gab_nil);                                          \
  }

#define GAB_ARRAYLIB_BINARY_CALL(_, f)                                         \
  GAB_DYNLIB_NATIVE_FN(array, f) {                                             \
    gab_value varr = gab_arg(0);                                               \
    gab_value vrhs = gab_arg(1);                                               \
    gab_value array_t = gab_string(gab, GAB_ARRAY);                            \
                                                                               \
    if (gab_valtype(gab, varr) != array_t)                                     \
      return gab_ptypemismatch(gab, varr, array_t);                            \
                                                                               \
    auto arr = unwrap<arrow::Array>(varr);                                     \
    auto rhs = valToDatum(gab, vrhs);                                          \
    if (!rhs.has_value())                                                      \
      return gab_panicf(gab, "Inalid rhs $", vrhs);                            \
    auto res = arrow::compute::CallFunction(#f, {arr, rhs.value()});           \
                                                                               \
    if (!res.ok())                                                             \
      return gab_panicf(gab, "$",                                              \
                        gab_string(gab, res.status().ToString().c_str()));     \
                                                                               \
    return gab_vmpush(gab_thisvm(gab),                                         \
                      datumToValue(gab, res.MoveValueUnsafe())),               \
           gab_union_cvalid(gab_nil);                                          \
  }

#define COMMA ,
#define SEMICOLON ;

// These X-macros are really ugly because they need to have different
// 'separators' (hence the s), as they are used to map declarations
// as well as expressions.
#define GAB_XARRAYLIB_UNARY(x, s)                                              \
  x("count", Count) s x("sum", Sum)                                            \
  s x("product", Product)                                                      \
  s x("stddev", Stddev)                                                        \
  s x("skew", Skew)                                                            \
  s x("variance", Variance)                                                    \
  s x("mean", Mean)                                                            \
  s x("mean\\cumul", CumulativeMean)                                           \
  s x("abs", AbsoluteValue)                                                    \
  s x("exp", Exp)                                                              \
  s x("negate", Negate)                                                        \
  s x("sqrt", Sqrt)                                                            \
  s x("ln", Ln)                                                                \
  s x("ceil", Ceil)                                                            \
  s x("floor", Floor)                                                          \
  s x("round", Round)                                                          \
  s x("trunc", Trunc)                                                          \
  s x("cos", Cos)                                                              \
  s x("sin", Sin)                                                              \
  s x("tan", Tan)                                                              \
  s x("acos", Acos)                                                            \
  s x("asin", Asin)                                                            \
  s x("atan", Atan)                                                            \
  s x("cosh", Cosh)                                                            \
  s x("sinh", Sinh)                                                            \
  s x("tanh", Tanh)                                                            \
  s x("acosh", Acosh)                                                          \
  s x("asinh", Asinh)                                                          \
  s x("atanh", Atanh)

#define GAB_XARRAYLIB_BINARY(x, s)                                             \
  x("*", Multiply) s x("/", Divide)                                            \
  s x("+", Add)                                                                \
  s x("-", Subtract)                                                           \
  s x("--", Power)                                                             \
  s x("&", And)                                                                \
  s x("|", Or)                                                                 \
  s x("^", Xor)                                                                \
  s x("<<", ShiftLeft)                                                         \
  s x(">>", ShiftRight)

#define GAB_XARRAYLIB_BINARYCALL(x, s) x("==", equal) s x("index", index)

GAB_XARRAYLIB_UNARY(GAB_ARRAYLIB_UNARY, SEMICOLON)
GAB_XARRAYLIB_BINARY(GAB_ARRAYLIB_BINARY, SEMICOLON)
GAB_XARRAYLIB_BINARYCALL(GAB_ARRAYLIB_BINARY_CALL, SEMICOLON)

GAB_DYNLIB_NATIVE_FN(array, tos) {
  gab_value varr = gab_arg(0);

  auto arr = unwrap<arrow::Array>(varr);

  gab_vmpush(gab_thisvm(gab), gab_string(gab, arr->ToString().c_str()));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(table, tos) {
  gab_value varr = gab_arg(0);

  auto table = unwrap<arrow::Table>(varr);

  gab_vmpush(gab_thisvm(gab), gab_string(gab, table->ToString().c_str()));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(table, open) {
  gab_value vpath = gab_arg(1);
  if (gab_valkind(vpath) != kGAB_STRING)
    return gab_pktypemismatch(gab, vpath, kGAB_STRING);

  auto res = GabInputStream::Open(gab, gab_strdata(&vpath));

  if (!res.ok())
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, res.status().ToString().c_str())),
           gab_union_cvalid(gab_nil);

  auto rf = res.MoveValueUnsafe();

  auto csv_res = arrow::csv::TableReader::Make(
      arrow::io::default_io_context(), rf, arrow::csv::ReadOptions(),
      arrow::csv::ParseOptions(), arrow::csv::ConvertOptions());

  if (!csv_res.ok())
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, csv_res.status().ToString().c_str())),
           gab_union_cvalid(gab_nil);

  auto csv_reader = csv_res.MoveValueUnsafe();

  auto table_res = csv_reader->Read();

  if (!table_res.ok())
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, table_res.status().ToString().c_str())),
           gab_union_cvalid(gab_nil);

  gab_vmpush(gab_thisvm(gab),
             wrap(gab, GAB_TABLE, table_res.MoveValueUnsafe()));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(table, create) {
  gab_value vschema = gab_arg(1);
  gab_value schema_t = gab_string(gab, GAB_SCHEMA);

  if (gab_valtype(gab, vschema) != schema_t)
    return gab_ptypemismatch(gab, vschema, schema_t);

  auto schema = unwrap<arrow::Schema>(vschema);

  gab_value array_t = gab_string(gab, GAB_ARRAY);
  std::vector<std::shared_ptr<arrow::Array>> columns = {};

  for (uint64_t i = 2; i < argc; i++) {
    gab_value col = gab_arg(i);

    if (gab_valtype(gab, col) != array_t)
      return gab_ptypemismatch(gab, col, array_t);

    auto arr = unwrap<arrow::Array>(col);
    columns.push_back(arr);
  }

  if (columns.size() != schema->num_fields())
    return gab_panicf(gab, "Schema with $ fields received $ columns",
                      gab_number(schema->num_fields()),
                      gab_number(columns.size()));

  auto table = arrow::Table::Make(schema, columns);
  table->GetColumnByName("x").get();
  gab_vmpush(gab_thisvm(gab), wrap<arrow::Table>(gab, GAB_TABLE, table));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(schema, create) {
  std::vector<std::shared_ptr<arrow::Field>> fields = {};

  uint64_t nargs = (argc - 1) / 2;
  for (uint64_t i = 0; i < nargs; i++) {
    uint64_t idx = (i * 2) + 1;
    gab_value name = gab_arg(idx);
    gab_value type = gab_arg(idx + 1);

    if (gab_valkind(name) != kGAB_STRING)
      return gab_pktypemismatch(gab, name, kGAB_STRING);

    if (gab_valkind(type) != kGAB_MESSAGE)
      return gab_pktypemismatch(gab, type, kGAB_MESSAGE);

    auto t = valToDatatype(gab, type);

    if (!t.has_value())
      return gab_panicf(gab, "$ is not a known datatype", type);

    auto field = std::make_shared<arrow::Field>(gab_strdata(&name), t.value());

    fields.emplace_back(field);
  }

  std::shared_ptr<arrow::Schema> schema = arrow::schema(fields);
  gab_value vschema = wrap<arrow::Schema>(gab, GAB_SCHEMA, schema);

  // for (uint64_t i = 0; i < nargs; i++) {
  //   uint64_t idx = (i * 2) + 1;
  //   gab_value name = gab_arg(idx);
  //   gab_def(gab, {
  //     gab_strtomsg(name),
  //     vschema,
  //     gab_
  //
  //   });
  // }

  gab_vmpush(gab_thisvm(gab), vschema);
  return gab_union_cvalid(gab_nil);
}

#define GAB_ARRAYBUILDER(builder, t, k, f)                                     \
  if (type == gab_message(gab, t)) {                                           \
    arrow::builder builder;                                                    \
    for (size_t i = 2; i < argc; i++) {                                        \
      gab_value nth = gab_arg(i);                                              \
      if (gab_valkind(nth) != k)                                               \
        return gab_pktypemismatch(gab, nth, k);                                \
                                                                               \
      arrow::Status s = builder.Append(f(nth));                                \
                                                                               \
      if (!s.ok())                                                             \
        return gab_panicf(gab, "$", gab_string(gab, s.ToString().c_str()));    \
    }                                                                          \
                                                                               \
    auto result = builder.Finish();                                            \
                                                                               \
    if (!result.ok())                                                          \
      return gab_panicf(gab, "$",                                              \
                        gab_string(gab, result.status().ToString().c_str()));  \
                                                                               \
    return gab_vmpush(                                                         \
               gab_thisvm(gab),                                                \
               wrap<arrow::Array>(gab, GAB_ARRAY, result.MoveValueUnsafe())),  \
           gab_union_cvalid(gab_nil);                                          \
  } else

#define getstrdata(str) gab_strdata(&str)

GAB_DYNLIB_NATIVE_FN(array, create) {
  gab_value type = gab_arg(1);

  GAB_ARRAYBUILDER(Int8Builder, GAB_DATATYPE_INT8, kGAB_NUMBER, gab_valtoi);
  GAB_ARRAYBUILDER(Int16Builder, GAB_DATATYPE_INT16, kGAB_NUMBER, gab_valtoi);
  GAB_ARRAYBUILDER(Int32Builder, GAB_DATATYPE_INT32, kGAB_NUMBER, gab_valtoi);
  GAB_ARRAYBUILDER(Int64Builder, GAB_DATATYPE_INT64, kGAB_NUMBER, gab_valtoi);

  GAB_ARRAYBUILDER(UInt8Builder, GAB_DATATYPE_UINT16, kGAB_NUMBER, gab_valtou);
  GAB_ARRAYBUILDER(UInt16Builder, GAB_DATATYPE_UINT16, kGAB_NUMBER, gab_valtou);
  GAB_ARRAYBUILDER(UInt32Builder, GAB_DATATYPE_UINT16, kGAB_NUMBER, gab_valtou);
  GAB_ARRAYBUILDER(UInt64Builder, GAB_DATATYPE_UINT16, kGAB_NUMBER, gab_valtou);

  GAB_ARRAYBUILDER(DoubleBuilder, GAB_DATATYPE_NUMBER, kGAB_NUMBER, gab_valtof);

  GAB_ARRAYBUILDER(BinaryBuilder, GAB_DATATYPE_BINARY, kGAB_BINARY, getstrdata);
  GAB_ARRAYBUILDER(StringBuilder, GAB_DATATYPE_STRING, kGAB_STRING, getstrdata);

  return gab_panicf(gab, "Invalid type for array: $", type);
}

#define GAB_ARRAYLIB_MESSAGE(m, f)                                             \
  {gab_message(gab, m), array_type, gab_snative(gab, m, gab_mod_array_##f)}

extern "C" {
GAB_DYNLIB_MAIN_FN {
  if (!arrow::compute::Initialize().ok())
    return gab_panicf(gab, "Failed to intialize apache-arrow");

  gab_value mod = gab_message(gab, GAB_DATAMOD);

  gab_value array_mod = gab_message(gab, GAB_ARRAY);
  gab_value array_type = gab_msgtostr(array_mod);

  gab_value schema_mod = gab_message(gab, GAB_SCHEMA);
  gab_value schema_type = gab_msgtostr(schema_mod);

  gab_value table_mod = gab_message(gab, GAB_TABLE);
  gab_value table_type = gab_msgtostr(table_mod);

  gab_value tensor_mod = gab_message(gab, GAB_TENSOR);
  gab_value tensor_type = gab_msgtostr(tensor_mod);

  gab_def(gab,
          {
              gab_message(gab, "Arrays"),
              mod,
              array_mod,
          },
          {
              gab_message(gab, "Schemas"),
              mod,
              schema_mod,
          },
          {
              gab_message(gab, "Tables"),
              mod,
              table_mod,
          },
          {
              gab_message(gab, "make"),
              array_mod,
              gab_snative(gab, "make", gab_mod_array_create),
          },
          {
              gab_message(gab, "t"),
              array_mod,
              array_type,
          },
          {
              gab_message(gab, "make"),
              schema_mod,
              gab_snative(gab, "make", gab_mod_schema_create),
          },
          {
              gab_message(gab, "t"),
              schema_mod,
              schema_type,
          },
          {
              gab_message(gab, "make"),
              table_mod,
              gab_snative(gab, "make", gab_mod_table_create),
          },
          {
              gab_message(gab, "t"),
              table_mod,
              table_type,
          },
          {
              gab_message(gab, "open"),
              table_mod,
              gab_snative(gab, "open", gab_mod_table_open),
          },
          GAB_XARRAYLIB_UNARY(GAB_ARRAYLIB_MESSAGE, COMMA),
          GAB_XARRAYLIB_BINARY(GAB_ARRAYLIB_MESSAGE, COMMA),
          GAB_XARRAYLIB_BINARYCALL(GAB_ARRAYLIB_MESSAGE, COMMA),
          GAB_ARRAYLIB_MESSAGE("to\\s", tos),
          {
              gab_message(gab, "to\\s"),
              table_type,
              gab_snative(gab, "to\\s", gab_mod_table_tos),
          }, );

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
}
