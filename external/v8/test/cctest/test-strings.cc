// Copyright 2012 the V8 project authors. All rights reserved.

// Check that we can traverse very deep stacks of ConsStrings using
// StringInputBuffer.  Check that Get(int) works on very deep stacks
// of ConsStrings.  These operations may not be very fast, but they
// should be possible without getting errors due to too deep recursion.

#include <stdlib.h>

#include "v8.h"

#include "api.h"
#include "factory.h"
#include "objects.h"
#include "cctest.h"
#include "zone-inl.h"

unsigned int seed = 123;

static uint32_t gen() {
        uint64_t z;
        z = seed;
        z *= 279470273;
        z %= 4294967291U;
        seed = static_cast<unsigned int>(z);
        return static_cast<uint32_t>(seed >> 16);
}


using namespace v8::internal;

static v8::Persistent<v8::Context> env;


static void InitializeVM() {
  if (env.IsEmpty()) {
    v8::HandleScope scope;
    const char* extensions[] = { "v8/print" };
    v8::ExtensionConfiguration config(1, extensions);
    env = v8::Context::New(&config);
  }
  v8::HandleScope scope;
  env->Enter();
}


static const int NUMBER_OF_BUILDING_BLOCKS = 128;
static const int DEEP_DEPTH = 8 * 1024;
static const int SUPER_DEEP_DEPTH = 80 * 1024;


class Resource: public v8::String::ExternalStringResource,
                public ZoneObject {
 public:
  explicit Resource(Vector<const uc16> string): data_(string.start()) {
    length_ = string.length();
  }
  virtual const uint16_t* data() const { return data_; }
  virtual size_t length() const { return length_; }

 private:
  const uc16* data_;
  size_t length_;
};


class AsciiResource: public v8::String::ExternalAsciiStringResource,
                public ZoneObject {
 public:
  explicit AsciiResource(Vector<const char> string): data_(string.start()) {
    length_ = string.length();
  }
  virtual const char* data() const { return data_; }
  virtual size_t length() const { return length_; }

 private:
  const char* data_;
  size_t length_;
};


static void InitializeBuildingBlocks(
    Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS]) {
  // A list of pointers that we don't have any interest in cleaning up.
  // If they are reachable from a root then leak detection won't complain.
  Zone* zone = Isolate::Current()->runtime_zone();
  for (int i = 0; i < NUMBER_OF_BUILDING_BLOCKS; i++) {
    int len = gen() % 16;
    if (len > 14) {
      len += 1234;
    }
    switch (gen() % 4) {
      case 0: {
        uc16 buf[2000];
        for (int j = 0; j < len; j++) {
          buf[j] = gen() % 65536;
        }
        building_blocks[i] =
            FACTORY->NewStringFromTwoByte(Vector<const uc16>(buf, len));
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
      case 1: {
        char buf[2000];
        for (int j = 0; j < len; j++) {
          buf[j] = gen() % 128;
        }
        building_blocks[i] =
            FACTORY->NewStringFromAscii(Vector<const char>(buf, len));
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
      case 2: {
        uc16* buf = zone->NewArray<uc16>(len);
        for (int j = 0; j < len; j++) {
          buf[j] = gen() % 65536;
        }
        Resource* resource = new(zone) Resource(Vector<const uc16>(buf, len));
        building_blocks[i] = FACTORY->NewExternalStringFromTwoByte(resource);
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
      case 3: {
        char* buf = NewArray<char>(len);
        for (int j = 0; j < len; j++) {
          buf[j] = gen() % 128;
        }
        building_blocks[i] =
            FACTORY->NewStringFromAscii(Vector<const char>(buf, len));
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        DeleteArray<char>(buf);
        break;
      }
    }
  }
}


static Handle<String> ConstructLeft(
    Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS],
    int depth) {
  Handle<String> answer = FACTORY->NewStringFromAscii(CStrVector(""));
  for (int i = 0; i < depth; i++) {
    answer = FACTORY->NewConsString(
        answer,
        building_blocks[i % NUMBER_OF_BUILDING_BLOCKS]);
  }
  return answer;
}


static Handle<String> ConstructRight(
    Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS],
    int depth) {
  Handle<String> answer = FACTORY->NewStringFromAscii(CStrVector(""));
  for (int i = depth - 1; i >= 0; i--) {
    answer = FACTORY->NewConsString(
        building_blocks[i % NUMBER_OF_BUILDING_BLOCKS],
        answer);
  }
  return answer;
}


static Handle<String> ConstructBalancedHelper(
    Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS],
    int from,
    int to) {
  CHECK(to > from);
  if (to - from == 1) {
    return building_blocks[from % NUMBER_OF_BUILDING_BLOCKS];
  }
  if (to - from == 2) {
    return FACTORY->NewConsString(
        building_blocks[from % NUMBER_OF_BUILDING_BLOCKS],
        building_blocks[(from+1) % NUMBER_OF_BUILDING_BLOCKS]);
  }
  Handle<String> part1 =
    ConstructBalancedHelper(building_blocks, from, from + ((to - from) / 2));
  Handle<String> part2 =
    ConstructBalancedHelper(building_blocks, from + ((to - from) / 2), to);
  return FACTORY->NewConsString(part1, part2);
}


static Handle<String> ConstructBalanced(
    Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS]) {
  return ConstructBalancedHelper(building_blocks, 0, DEEP_DEPTH);
}


static StringInputBuffer buffer;


static void Traverse(Handle<String> s1, Handle<String> s2) {
  int i = 0;
  buffer.Reset(*s1);
  StringInputBuffer buffer2(*s2);
  while (buffer.has_more()) {
    CHECK(buffer2.has_more());
    uint16_t c = buffer.GetNext();
    CHECK_EQ(c, buffer2.GetNext());
    i++;
  }
  CHECK_EQ(s1->length(), i);
  CHECK_EQ(s2->length(), i);
}


static void TraverseFirst(Handle<String> s1, Handle<String> s2, int chars) {
  int i = 0;
  buffer.Reset(*s1);
  StringInputBuffer buffer2(*s2);
  while (buffer.has_more() && i < chars) {
    CHECK(buffer2.has_more());
    uint16_t c = buffer.GetNext();
    CHECK_EQ(c, buffer2.GetNext());
    i++;
  }
  s1->Get(s1->length() - 1);
  s2->Get(s2->length() - 1);
}


TEST(Traverse) {
  printf("TestTraverse\n");
  InitializeVM();
  v8::HandleScope scope;
  Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS];
  ZoneScope zone(Isolate::Current()->runtime_zone(), DELETE_ON_EXIT);
  InitializeBuildingBlocks(building_blocks);
  Handle<String> flat = ConstructBalanced(building_blocks);
  FlattenString(flat);
  Handle<String> left_asymmetric = ConstructLeft(building_blocks, DEEP_DEPTH);
  Handle<String> right_asymmetric = ConstructRight(building_blocks, DEEP_DEPTH);
  Handle<String> symmetric = ConstructBalanced(building_blocks);
  printf("1\n");
  Traverse(flat, symmetric);
  printf("2\n");
  Traverse(flat, left_asymmetric);
  printf("3\n");
  Traverse(flat, right_asymmetric);
  printf("4\n");
  Handle<String> left_deep_asymmetric =
      ConstructLeft(building_blocks, SUPER_DEEP_DEPTH);
  Handle<String> right_deep_asymmetric =
      ConstructRight(building_blocks, SUPER_DEEP_DEPTH);
  printf("5\n");
  TraverseFirst(left_asymmetric, left_deep_asymmetric, 1050);
  printf("6\n");
  TraverseFirst(left_asymmetric, right_deep_asymmetric, 65536);
  printf("7\n");
  FlattenString(left_asymmetric);
  printf("10\n");
  Traverse(flat, left_asymmetric);
  printf("11\n");
  FlattenString(right_asymmetric);
  printf("12\n");
  Traverse(flat, right_asymmetric);
  printf("14\n");
  FlattenString(symmetric);
  printf("15\n");
  Traverse(flat, symmetric);
  printf("16\n");
  FlattenString(left_deep_asymmetric);
  printf("18\n");
}


static const int DEEP_ASCII_DEPTH = 100000;


TEST(DeepAscii) {
  printf("TestDeepAscii\n");
  InitializeVM();
  v8::HandleScope scope;

  char* foo = NewArray<char>(DEEP_ASCII_DEPTH);
  for (int i = 0; i < DEEP_ASCII_DEPTH; i++) {
    foo[i] = "foo "[i % 4];
  }
  Handle<String> string =
      FACTORY->NewStringFromAscii(Vector<const char>(foo, DEEP_ASCII_DEPTH));
  Handle<String> foo_string = FACTORY->NewStringFromAscii(CStrVector("foo"));
  for (int i = 0; i < DEEP_ASCII_DEPTH; i += 10) {
    string = FACTORY->NewConsString(string, foo_string);
  }
  Handle<String> flat_string = FACTORY->NewConsString(string, foo_string);
  FlattenString(flat_string);

  for (int i = 0; i < 500; i++) {
    TraverseFirst(flat_string, string, DEEP_ASCII_DEPTH);
  }
  DeleteArray<char>(foo);
}


TEST(Utf8Conversion) {
  // Smoke test for converting strings to utf-8.
  InitializeVM();
  v8::HandleScope handle_scope;
  // A simple ascii string
  const char* ascii_string = "abcdef12345";
  int len =
      v8::String::New(ascii_string,
                      StrLength(ascii_string))->Utf8Length();
  CHECK_EQ(StrLength(ascii_string), len);
  // A mixed ascii and non-ascii string
  // U+02E4 -> CB A4
  // U+0064 -> 64
  // U+12E4 -> E1 8B A4
  // U+0030 -> 30
  // U+3045 -> E3 81 85
  const uint16_t mixed_string[] = {0x02E4, 0x0064, 0x12E4, 0x0030, 0x3045};
  // The characters we expect to be output
  const unsigned char as_utf8[11] = {0xCB, 0xA4, 0x64, 0xE1, 0x8B, 0xA4, 0x30,
      0xE3, 0x81, 0x85, 0x00};
  // The number of bytes expected to be written for each length
  const int lengths[12] = {0, 0, 2, 3, 3, 3, 6, 7, 7, 7, 10, 11};
  const int char_lengths[12] = {0, 0, 1, 2, 2, 2, 3, 4, 4, 4, 5, 5};
  v8::Handle<v8::String> mixed = v8::String::New(mixed_string, 5);
  CHECK_EQ(10, mixed->Utf8Length());
  // Try encoding the string with all capacities
  char buffer[11];
  const char kNoChar = static_cast<char>(-1);
  for (int i = 0; i <= 11; i++) {
    // Clear the buffer before reusing it
    for (int j = 0; j < 11; j++)
      buffer[j] = kNoChar;
    int chars_written;
    int written = mixed->WriteUtf8(buffer, i, &chars_written);
    CHECK_EQ(lengths[i], written);
    CHECK_EQ(char_lengths[i], chars_written);
    // Check that the contents are correct
    for (int j = 0; j < lengths[i]; j++)
      CHECK_EQ(as_utf8[j], static_cast<unsigned char>(buffer[j]));
    // Check that the rest of the buffer hasn't been touched
    for (int j = lengths[i]; j < 11; j++)
      CHECK_EQ(kNoChar, buffer[j]);
  }
}


TEST(ExternalShortStringAdd) {
  ZoneScope zonescope(Isolate::Current()->runtime_zone(), DELETE_ON_EXIT);

  InitializeVM();
  v8::HandleScope handle_scope;
  Zone* zone = Isolate::Current()->runtime_zone();

  // Make sure we cover all always-flat lengths and at least one above.
  static const int kMaxLength = 20;
  CHECK_GT(kMaxLength, i::ConsString::kMinLength);

  // Allocate two JavaScript arrays for holding short strings.
  v8::Handle<v8::Array> ascii_external_strings =
      v8::Array::New(kMaxLength + 1);
  v8::Handle<v8::Array> non_ascii_external_strings =
      v8::Array::New(kMaxLength + 1);

  // Generate short ascii and non-ascii external strings.
  for (int i = 0; i <= kMaxLength; i++) {
    char* ascii = zone->NewArray<char>(i + 1);
    for (int j = 0; j < i; j++) {
      ascii[j] = 'a';
    }
    // Terminating '\0' is left out on purpose. It is not required for external
    // string data.
    AsciiResource* ascii_resource =
        new(zone) AsciiResource(Vector<const char>(ascii, i));
    v8::Local<v8::String> ascii_external_string =
        v8::String::NewExternal(ascii_resource);

    ascii_external_strings->Set(v8::Integer::New(i), ascii_external_string);
    uc16* non_ascii = zone->NewArray<uc16>(i + 1);
    for (int j = 0; j < i; j++) {
      non_ascii[j] = 0x1234;
    }
    // Terminating '\0' is left out on purpose. It is not required for external
    // string data.
    Resource* resource = new(zone) Resource(Vector<const uc16>(non_ascii, i));
    v8::Local<v8::String> non_ascii_external_string =
      v8::String::NewExternal(resource);
    non_ascii_external_strings->Set(v8::Integer::New(i),
                                    non_ascii_external_string);
  }

  // Add the arrays with the short external strings in the global object.
  v8::Handle<v8::Object> global = env->Global();
  global->Set(v8_str("external_ascii"), ascii_external_strings);
  global->Set(v8_str("external_non_ascii"), non_ascii_external_strings);
  global->Set(v8_str("max_length"), v8::Integer::New(kMaxLength));

  // Add short external ascii and non-ascii strings checking the result.
  static const char* source =
    "function test() {"
    "  var ascii_chars = 'aaaaaaaaaaaaaaaaaaaa';"
    "  var non_ascii_chars = '\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234';"  //NOLINT
    "  if (ascii_chars.length != max_length) return 1;"
    "  if (non_ascii_chars.length != max_length) return 2;"
    "  var ascii = Array(max_length + 1);"
    "  var non_ascii = Array(max_length + 1);"
    "  for (var i = 0; i <= max_length; i++) {"
    "    ascii[i] = ascii_chars.substring(0, i);"
    "    non_ascii[i] = non_ascii_chars.substring(0, i);"
    "  };"
    "  for (var i = 0; i <= max_length; i++) {"
    "    if (ascii[i] != external_ascii[i]) return 3;"
    "    if (non_ascii[i] != external_non_ascii[i]) return 4;"
    "    for (var j = 0; j < i; j++) {"
    "      if (external_ascii[i] !="
    "          (external_ascii[j] + external_ascii[i - j])) return 5;"
    "      if (external_non_ascii[i] !="
    "          (external_non_ascii[j] + external_non_ascii[i - j])) return 6;"
    "      if (non_ascii[i] != (non_ascii[j] + non_ascii[i - j])) return 7;"
    "      if (ascii[i] != (ascii[j] + ascii[i - j])) return 8;"
    "      if (ascii[i] != (external_ascii[j] + ascii[i - j])) return 9;"
    "      if (ascii[i] != (ascii[j] + external_ascii[i - j])) return 10;"
    "      if (non_ascii[i] !="
    "          (external_non_ascii[j] + non_ascii[i - j])) return 11;"
    "      if (non_ascii[i] !="
    "          (non_ascii[j] + external_non_ascii[i - j])) return 12;"
    "    }"
    "  }"
    "  return 0;"
    "};"
    "test()";
  CHECK_EQ(0, CompileRun(source)->Int32Value());
}


TEST(CachedHashOverflow) {
  // We incorrectly allowed strings to be tagged as array indices even if their
  // values didn't fit in the hash field.
  // See http://code.google.com/p/v8/issues/detail?id=728
  ZoneScope zone(Isolate::Current()->runtime_zone(), DELETE_ON_EXIT);

  InitializeVM();
  v8::HandleScope handle_scope;
  // Lines must be executed sequentially. Combining them into one script
  // makes the bug go away.
  const char* lines[] = {
      "var x = [];",
      "x[4] = 42;",
      "var s = \"1073741828\";",
      "x[s];",
      "x[s] = 37;",
      "x[4];",
      "x[s];",
      NULL
  };

  Handle<Smi> fortytwo(Smi::FromInt(42));
  Handle<Smi> thirtyseven(Smi::FromInt(37));
  Handle<Object> results[] = {
      FACTORY->undefined_value(),
      fortytwo,
      FACTORY->undefined_value(),
      FACTORY->undefined_value(),
      thirtyseven,
      fortytwo,
      thirtyseven  // Bug yielded 42 here.
  };

  const char* line;
  for (int i = 0; (line = lines[i]); i++) {
    printf("%s\n", line);
    v8::Local<v8::Value> result =
        v8::Script::Compile(v8::String::New(line))->Run();
    CHECK_EQ(results[i]->IsUndefined(), result->IsUndefined());
    CHECK_EQ(results[i]->IsNumber(), result->IsNumber());
    if (result->IsNumber()) {
      CHECK_EQ(Smi::cast(results[i]->ToSmi()->ToObjectChecked())->value(),
               result->ToInt32()->Value());
    }
  }
}


TEST(SliceFromCons) {
  FLAG_string_slices = true;
  InitializeVM();
  v8::HandleScope scope;
  Handle<String> string =
      FACTORY->NewStringFromAscii(CStrVector("parentparentparent"));
  Handle<String> parent = FACTORY->NewConsString(string, string);
  CHECK(parent->IsConsString());
  CHECK(!parent->IsFlat());
  Handle<String> slice = FACTORY->NewSubString(parent, 1, 25);
  // After slicing, the original string becomes a flat cons.
  CHECK(parent->IsFlat());
  CHECK(slice->IsSlicedString());
  CHECK_EQ(SlicedString::cast(*slice)->parent(),
           ConsString::cast(*parent)->first());
  CHECK(SlicedString::cast(*slice)->parent()->IsSeqString());
  CHECK(slice->IsFlat());
}


class AsciiVectorResource : public v8::String::ExternalAsciiStringResource {
 public:
  explicit AsciiVectorResource(i::Vector<const char> vector)
      : data_(vector) {}
  virtual ~AsciiVectorResource() {}
  virtual size_t length() const { return data_.length(); }
  virtual const char* data() const { return data_.start(); }
 private:
  i::Vector<const char> data_;
};


TEST(SliceFromExternal) {
  FLAG_string_slices = true;
  InitializeVM();
  v8::HandleScope scope;
  AsciiVectorResource resource(
      i::Vector<const char>("abcdefghijklmnopqrstuvwxyz", 26));
  Handle<String> string = FACTORY->NewExternalStringFromAscii(&resource);
  CHECK(string->IsExternalString());
  Handle<String> slice = FACTORY->NewSubString(string, 1, 25);
  CHECK(slice->IsSlicedString());
  CHECK(string->IsExternalString());
  CHECK_EQ(SlicedString::cast(*slice)->parent(), *string);
  CHECK(SlicedString::cast(*slice)->parent()->IsExternalString());
  CHECK(slice->IsFlat());
}


TEST(TrivialSlice) {
  // This tests whether a slice that contains the entire parent string
  // actually creates a new string (it should not).
  FLAG_string_slices = true;
  InitializeVM();
  HandleScope scope;
  v8::Local<v8::Value> result;
  Handle<String> string;
  const char* init = "var str = 'abcdefghijklmnopqrstuvwxyz';";
  const char* check = "str.slice(0,26)";
  const char* crosscheck = "str.slice(1,25)";

  CompileRun(init);

  result = CompileRun(check);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(!string->IsSlicedString());

  string = FACTORY->NewSubString(string, 0, 26);
  CHECK(!string->IsSlicedString());
  result = CompileRun(crosscheck);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(string->IsSlicedString());
  CHECK_EQ("bcdefghijklmnopqrstuvwxy", *(string->ToCString()));
}


TEST(SliceFromSlice) {
  // This tests whether a slice that contains the entire parent string
  // actually creates a new string (it should not).
  FLAG_string_slices = true;
  InitializeVM();
  HandleScope scope;
  v8::Local<v8::Value> result;
  Handle<String> string;
  const char* init = "var str = 'abcdefghijklmnopqrstuvwxyz';";
  const char* slice = "var slice = str.slice(1,-1); slice";
  const char* slice_from_slice = "slice.slice(1,-1);";

  CompileRun(init);
  result = CompileRun(slice);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(string->IsSlicedString());
  CHECK(SlicedString::cast(*string)->parent()->IsSeqString());
  CHECK_EQ("bcdefghijklmnopqrstuvwxy", *(string->ToCString()));

  result = CompileRun(slice_from_slice);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(string->IsSlicedString());
  CHECK(SlicedString::cast(*string)->parent()->IsSeqString());
  CHECK_EQ("cdefghijklmnopqrstuvwx", *(string->ToCString()));
}


TEST(AsciiArrayJoin) {
  // Set heap limits.
  static const int K = 1024;
  v8::ResourceConstraints constraints;
  constraints.set_max_young_space_size(256 * K);
  constraints.set_max_old_space_size(4 * K * K);
  v8::SetResourceConstraints(&constraints);

  // String s is made of 2^17 = 131072 'c' characters and a is an array
  // starting with 'bad', followed by 2^14 times the string s. That means the
  // total length of the concatenated strings is 2^31 + 3. So on 32bit systems
  // summing the lengths of the strings (as Smis) overflows and wraps.
  static const char* join_causing_out_of_memory =
      "var two_14 = Math.pow(2, 14);"
      "var two_17 = Math.pow(2, 17);"
      "var s = Array(two_17 + 1).join('c');"
      "var a = ['bad'];"
      "for (var i = 1; i <= two_14; i++) a.push(s);"
      "a.join("");";

  v8::HandleScope scope;
  LocalContext context;
  v8::V8::IgnoreOutOfMemoryException();
  v8::Local<v8::Script> script =
      v8::Script::Compile(v8::String::New(join_causing_out_of_memory));
  v8::Local<v8::Value> result = script->Run();

  // Check for out of memory state.
  CHECK(result.IsEmpty());
  CHECK(context->HasOutOfMemoryException());
}


static void CheckException(const char* source) {
  // An empty handle is returned upon exception.
  CHECK(CompileRun(source).IsEmpty());
}


TEST(RobustSubStringStub) {
  // This tests whether the SubStringStub can handle unsafe arguments.
  // If not recognized, those unsafe arguments lead to out-of-bounds reads.
  FLAG_allow_natives_syntax = true;
  InitializeVM();
  HandleScope scope;
  v8::Local<v8::Value> result;
  Handle<String> string;
  CompileRun("var short = 'abcdef';");

  // Invalid indices.
  CheckException("%_SubString(short,     0,    10000);");
  CheckException("%_SubString(short, -1234,        5);");
  CheckException("%_SubString(short,     5,        2);");
  // Special HeapNumbers.
  CheckException("%_SubString(short,     1, Infinity);");
  CheckException("%_SubString(short,   NaN,        5);");
  // String arguments.
  CheckException("%_SubString(short,    '2',     '5');");
  // Ordinary HeapNumbers can be handled (in runtime).
  result = CompileRun("%_SubString(short, Math.sqrt(4), 5.1);");
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK_EQ("cde", *(string->ToCString()));

  CompileRun("var long = 'abcdefghijklmnopqrstuvwxyz';");
  // Invalid indices.
  CheckException("%_SubString(long,     0,    10000);");
  CheckException("%_SubString(long, -1234,       17);");
  CheckException("%_SubString(long,    17,        2);");
  // Special HeapNumbers.
  CheckException("%_SubString(long,     1, Infinity);");
  CheckException("%_SubString(long,   NaN,       17);");
  // String arguments.
  CheckException("%_SubString(long,    '2',    '17');");
  // Ordinary HeapNumbers within bounds can be handled (in runtime).
  result = CompileRun("%_SubString(long, Math.sqrt(4), 17.1);");
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK_EQ("cdefghijklmnopq", *(string->ToCString()));

  // Test that out-of-bounds substring of a slice fails when the indices
  // would have been valid for the underlying string.
  CompileRun("var slice = long.slice(1, 15);");
  CheckException("%_SubString(slice, 0, 17);");
}


TEST(RegExpOverflow) {
  // Result string has the length 2^32, causing a 32-bit integer overflow.
  InitializeVM();
  HandleScope scope;
  LocalContext context;
  v8::V8::IgnoreOutOfMemoryException();
  v8::Local<v8::Value> result = CompileRun(
      "var a = 'a';                     "
      "for (var i = 0; i < 16; i++) {   "
      "  a += a;                        "
      "}                                "
      "a.replace(/a/g, a);              ");
  CHECK(result.IsEmpty());
  CHECK(context->HasOutOfMemoryException());
}


TEST(StringReplaceAtomTwoByteResult) {
  InitializeVM();
  HandleScope scope;
  LocalContext context;
  v8::Local<v8::Value> result = CompileRun(
      "var subject = 'ascii~only~string~'; "
      "var replace = '\x80';            "
      "subject.replace(/~/g, replace);  ");
  CHECK(result->IsString());
  Handle<String> string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(string->IsSeqTwoByteString());

  v8::Local<v8::String> expected = v8_str("ascii\x80only\x80string\x80");
  CHECK(expected->Equals(result));
}


TEST(IsAscii) {
  CHECK(String::IsAscii(static_cast<char*>(NULL), 0));
  CHECK(String::IsAscii(static_cast<uc16*>(NULL), 0));
}
