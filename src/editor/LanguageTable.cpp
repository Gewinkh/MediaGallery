#include "editor/LanguageTable.h"

#include <QHash>
#include <algorithm>

// Die Wortlisten wurden einmal sortiert erzeugt und werden seitdem von Hand gepflegt. Jede Liste MUSS sortiert
// bleiben - `containsWord` sucht binär; `tst_editor_langtable` prüft das. Neue Sprache = ein `LanguageDef` mehr.
using namespace Qt::Literals::StringLiterals;

namespace mg::editor {
namespace {

static const QLatin1StringView k_kw_cmake[] = {
    "add_custom_command"_L1, "add_custom_target"_L1, "add_definitions"_L1, "add_executable"_L1,
    "add_library"_L1, "add_subdirectory"_L1, "add_test"_L1, "break"_L1,
    "cmake_minimum_required"_L1, "continue"_L1, "else"_L1, "elseif"_L1, "enable_testing"_L1,
    "endforeach"_L1, "endfunction"_L1, "endif"_L1, "endmacro"_L1, "endwhile"_L1, "file"_L1,
    "find_package"_L1, "find_program"_L1, "foreach"_L1, "function"_L1,
    "get_target_property"_L1, "if"_L1, "include"_L1, "install"_L1, "list"_L1, "macro"_L1,
    "message"_L1, "option"_L1, "project"_L1, "return"_L1, "set"_L1, "set_target_properties"_L1,
    "string"_L1, "target_compile_definitions"_L1, "target_compile_options"_L1,
    "target_include_directories"_L1, "target_link_libraries"_L1, "unset"_L1, "while"_L1
};
static const QLatin1StringView k_kw_cpp[] = {
    "alignas"_L1, "alignof"_L1, "and"_L1, "and_eq"_L1, "asm"_L1, "auto"_L1, "bitand"_L1,
    "bitor"_L1, "bool"_L1, "break"_L1, "case"_L1, "catch"_L1, "char"_L1, "char16_t"_L1,
    "char32_t"_L1, "char8_t"_L1, "class"_L1, "co_await"_L1, "co_return"_L1, "co_yield"_L1,
    "compl"_L1, "concept"_L1, "const"_L1, "const_cast"_L1, "consteval"_L1, "constexpr"_L1,
    "constinit"_L1, "continue"_L1, "decltype"_L1, "default"_L1, "delete"_L1, "do"_L1,
    "double"_L1, "dynamic_cast"_L1, "else"_L1, "enum"_L1, "explicit"_L1, "export"_L1,
    "extern"_L1, "false"_L1, "float"_L1, "for"_L1, "friend"_L1, "goto"_L1, "if"_L1,
    "inline"_L1, "int"_L1, "long"_L1, "mutable"_L1, "namespace"_L1, "new"_L1, "noexcept"_L1,
    "not"_L1, "not_eq"_L1, "nullptr"_L1, "operator"_L1, "or"_L1, "or_eq"_L1, "private"_L1,
    "protected"_L1, "public"_L1, "register"_L1, "reinterpret_cast"_L1, "requires"_L1,
    "return"_L1, "short"_L1, "signed"_L1, "sizeof"_L1, "static"_L1, "static_assert"_L1,
    "static_cast"_L1, "struct"_L1, "switch"_L1, "template"_L1, "this"_L1, "thread_local"_L1,
    "throw"_L1, "true"_L1, "try"_L1, "typedef"_L1, "typeid"_L1, "typename"_L1, "union"_L1,
    "unsigned"_L1, "using"_L1, "virtual"_L1, "void"_L1, "volatile"_L1, "wchar_t"_L1,
    "while"_L1, "xor"_L1, "xor_eq"_L1
};
static const QLatin1StringView k_kw_csharp[] = {
    "abstract"_L1, "as"_L1, "async"_L1, "await"_L1, "base"_L1, "bool"_L1, "break"_L1,
    "byte"_L1, "case"_L1, "catch"_L1, "char"_L1, "checked"_L1, "class"_L1, "const"_L1,
    "continue"_L1, "decimal"_L1, "default"_L1, "delegate"_L1, "do"_L1, "double"_L1, "else"_L1,
    "enum"_L1, "event"_L1, "explicit"_L1, "extern"_L1, "false"_L1, "finally"_L1, "fixed"_L1,
    "float"_L1, "for"_L1, "foreach"_L1, "goto"_L1, "if"_L1, "implicit"_L1, "in"_L1, "init"_L1,
    "int"_L1, "interface"_L1, "internal"_L1, "is"_L1, "lock"_L1, "long"_L1, "namespace"_L1,
    "new"_L1, "nint"_L1, "nuint"_L1, "null"_L1, "object"_L1, "operator"_L1, "out"_L1,
    "override"_L1, "params"_L1, "private"_L1, "protected"_L1, "public"_L1, "readonly"_L1,
    "record"_L1, "ref"_L1, "return"_L1, "sbyte"_L1, "sealed"_L1, "short"_L1, "sizeof"_L1,
    "stackalloc"_L1, "static"_L1, "string"_L1, "struct"_L1, "switch"_L1, "this"_L1, "throw"_L1,
    "true"_L1, "try"_L1, "typeof"_L1, "uint"_L1, "ulong"_L1, "unchecked"_L1, "unsafe"_L1,
    "ushort"_L1, "using"_L1, "var"_L1, "virtual"_L1, "void"_L1, "volatile"_L1, "while"_L1
};
static const QLatin1StringView k_kw_go[] = {
    "break"_L1, "case"_L1, "chan"_L1, "const"_L1, "continue"_L1, "default"_L1, "defer"_L1,
    "else"_L1, "fallthrough"_L1, "false"_L1, "for"_L1, "func"_L1, "go"_L1, "goto"_L1, "if"_L1,
    "import"_L1, "interface"_L1, "iota"_L1, "map"_L1, "nil"_L1, "package"_L1, "range"_L1,
    "return"_L1, "select"_L1, "struct"_L1, "switch"_L1, "true"_L1, "type"_L1, "var"_L1
};
static const QLatin1StringView k_kw_java[] = {
    "abstract"_L1, "assert"_L1, "boolean"_L1, "break"_L1, "byte"_L1, "case"_L1, "catch"_L1,
    "char"_L1, "class"_L1, "const"_L1, "continue"_L1, "default"_L1, "do"_L1, "double"_L1,
    "else"_L1, "enum"_L1, "extends"_L1, "false"_L1, "final"_L1, "finally"_L1, "float"_L1,
    "for"_L1, "goto"_L1, "if"_L1, "implements"_L1, "import"_L1, "instanceof"_L1, "int"_L1,
    "interface"_L1, "long"_L1, "native"_L1, "new"_L1, "null"_L1, "package"_L1, "private"_L1,
    "protected"_L1, "public"_L1, "record"_L1, "return"_L1, "sealed"_L1, "short"_L1,
    "static"_L1, "strictfp"_L1, "super"_L1, "switch"_L1, "synchronized"_L1, "this"_L1,
    "throw"_L1, "throws"_L1, "transient"_L1, "true"_L1, "try"_L1, "var"_L1, "void"_L1,
    "volatile"_L1, "while"_L1, "yield"_L1
};
static const QLatin1StringView k_kw_js[] = {
    "abstract"_L1, "any"_L1, "as"_L1, "asserts"_L1, "async"_L1, "await"_L1, "boolean"_L1,
    "break"_L1, "case"_L1, "catch"_L1, "class"_L1, "const"_L1, "continue"_L1, "debugger"_L1,
    "declare"_L1, "default"_L1, "delete"_L1, "do"_L1, "else"_L1, "enum"_L1, "export"_L1,
    "extends"_L1, "false"_L1, "finally"_L1, "for"_L1, "from"_L1, "function"_L1, "get"_L1,
    "if"_L1, "implements"_L1, "import"_L1, "in"_L1, "infer"_L1, "instanceof"_L1,
    "interface"_L1, "is"_L1, "keyof"_L1, "let"_L1, "module"_L1, "namespace"_L1, "never"_L1,
    "new"_L1, "null"_L1, "number"_L1, "object"_L1, "of"_L1, "private"_L1, "protected"_L1,
    "public"_L1, "readonly"_L1, "require"_L1, "return"_L1, "set"_L1, "static"_L1, "string"_L1,
    "super"_L1, "switch"_L1, "symbol"_L1, "this"_L1, "throw"_L1, "true"_L1, "try"_L1,
    "type"_L1, "typeof"_L1, "undefined"_L1, "unique"_L1, "unknown"_L1, "var"_L1, "void"_L1,
    "while"_L1, "with"_L1, "yield"_L1
};
static const QLatin1StringView k_kw_kotlin[] = {
    "abstract"_L1, "actual"_L1, "annotation"_L1, "as"_L1, "break"_L1, "by"_L1, "catch"_L1,
    "class"_L1, "companion"_L1, "const"_L1, "constructor"_L1, "continue"_L1, "crossinline"_L1,
    "data"_L1, "delegate"_L1, "do"_L1, "dynamic"_L1, "else"_L1, "enum"_L1, "expect"_L1,
    "external"_L1, "false"_L1, "final"_L1, "finally"_L1, "for"_L1, "fun"_L1, "get"_L1, "if"_L1,
    "import"_L1, "in"_L1, "infix"_L1, "init"_L1, "inline"_L1, "inner"_L1, "interface"_L1,
    "internal"_L1, "is"_L1, "lateinit"_L1, "lazy"_L1, "noinline"_L1, "null"_L1, "object"_L1,
    "open"_L1, "operator"_L1, "out"_L1, "override"_L1, "package"_L1, "private"_L1,
    "protected"_L1, "public"_L1, "reified"_L1, "return"_L1, "sealed"_L1, "set"_L1, "super"_L1,
    "suspend"_L1, "tailrec"_L1, "this"_L1, "throw"_L1, "true"_L1, "try"_L1, "typealias"_L1,
    "val"_L1, "var"_L1, "vararg"_L1, "when"_L1, "where"_L1, "while"_L1
};
static const QLatin1StringView k_kw_dart[] = {
    "abstract"_L1, "as"_L1, "assert"_L1, "async"_L1, "await"_L1, "break"_L1,
    "case"_L1, "catch"_L1, "class"_L1, "const"_L1, "continue"_L1, "covariant"_L1,
    "default"_L1, "deferred"_L1, "do"_L1, "else"_L1, "enum"_L1, "export"_L1,
    "extends"_L1, "extension"_L1, "external"_L1, "factory"_L1, "false"_L1,
    "final"_L1, "finally"_L1, "for"_L1, "get"_L1, "if"_L1, "implements"_L1,
    "import"_L1, "in"_L1, "is"_L1, "late"_L1, "library"_L1, "mixin"_L1, "new"_L1,
    "null"_L1, "on"_L1, "operator"_L1, "part"_L1, "required"_L1, "rethrow"_L1,
    "return"_L1, "set"_L1, "show"_L1, "static"_L1, "super"_L1, "switch"_L1,
    "sync"_L1, "this"_L1, "throw"_L1, "true"_L1, "try"_L1, "typedef"_L1,
    "var"_L1, "while"_L1, "with"_L1, "yield"_L1
};
static const QLatin1StringView k_ty_dart[] = {
    "List"_L1, "Map"_L1, "Object"_L1, "Set"_L1, "String"_L1, "bool"_L1,
    "double"_L1, "dynamic"_L1, "int"_L1, "num"_L1, "void"_L1
};

static const QLatin1StringView k_kw_perl[] = {
    "and"_L1, "bless"_L1, "do"_L1, "each"_L1, "else"_L1, "elsif"_L1, "eq"_L1,
    "eval"_L1, "exists"_L1, "for"_L1, "foreach"_L1, "ge"_L1, "gt"_L1, "if"_L1,
    "keys"_L1, "last"_L1, "le"_L1, "local"_L1, "lt"_L1, "my"_L1, "ne"_L1,
    "next"_L1, "no"_L1, "not"_L1, "or"_L1, "our"_L1, "package"_L1, "print"_L1,
    "push"_L1, "redo"_L1, "ref"_L1, "require"_L1, "return"_L1, "shift"_L1,
    "sort"_L1, "sub"_L1, "unless"_L1, "until"_L1, "use"_L1, "values"_L1,
    "wantarray"_L1, "while"_L1
};

static const QLatin1StringView k_kw_r[] = {
    "break"_L1, "else"_L1, "for"_L1, "function"_L1, "if"_L1, "in"_L1,
    "library"_L1, "next"_L1, "repeat"_L1, "require"_L1, "return"_L1, "while"_L1
};
static const QLatin1StringView k_ty_r[] = {
    "FALSE"_L1, "Inf"_L1, "NA"_L1, "NULL"_L1, "NaN"_L1, "TRUE"_L1,
    "character"_L1, "complex"_L1, "data.frame"_L1, "double"_L1, "factor"_L1,
    "integer"_L1, "list"_L1, "logical"_L1, "matrix"_L1, "numeric"_L1, "vector"_L1
};

static const QLatin1StringView k_kw_lua[] = {
    "and"_L1, "break"_L1, "do"_L1, "else"_L1, "elseif"_L1, "end"_L1, "false"_L1, "for"_L1,
    "function"_L1, "goto"_L1, "if"_L1, "in"_L1, "local"_L1, "nil"_L1, "not"_L1, "or"_L1,
    "repeat"_L1, "return"_L1, "then"_L1, "true"_L1, "until"_L1, "while"_L1
};
static const QLatin1StringView k_kw_php[] = {
    "abstract"_L1, "and"_L1, "array"_L1, "as"_L1, "break"_L1, "callable"_L1, "case"_L1,
    "catch"_L1, "class"_L1, "clone"_L1, "const"_L1, "continue"_L1, "declare"_L1, "default"_L1,
    "do"_L1, "echo"_L1, "else"_L1, "elseif"_L1, "empty"_L1, "enddeclare"_L1, "endfor"_L1,
    "endforeach"_L1, "endif"_L1, "endswitch"_L1, "endwhile"_L1, "enum"_L1, "extends"_L1,
    "false"_L1, "final"_L1, "finally"_L1, "fn"_L1, "for"_L1, "foreach"_L1, "function"_L1,
    "global"_L1, "goto"_L1, "if"_L1, "implements"_L1, "include"_L1, "include_once"_L1,
    "instanceof"_L1, "insteadof"_L1, "interface"_L1, "isset"_L1, "list"_L1, "match"_L1,
    "namespace"_L1, "new"_L1, "null"_L1, "or"_L1, "print"_L1, "private"_L1, "protected"_L1,
    "public"_L1, "readonly"_L1, "require"_L1, "require_once"_L1, "return"_L1, "static"_L1,
    "switch"_L1, "throw"_L1, "trait"_L1, "true"_L1, "try"_L1, "unset"_L1, "use"_L1, "var"_L1,
    "while"_L1, "xor"_L1, "yield"_L1
};
static const QLatin1StringView k_kw_python[] = {
    "False"_L1, "None"_L1, "True"_L1, "and"_L1, "as"_L1, "assert"_L1, "async"_L1, "await"_L1,
    "break"_L1, "case"_L1, "class"_L1, "continue"_L1, "def"_L1, "del"_L1, "elif"_L1, "else"_L1,
    "except"_L1, "finally"_L1, "for"_L1, "from"_L1, "global"_L1, "if"_L1, "import"_L1, "in"_L1,
    "is"_L1, "lambda"_L1, "match"_L1, "nonlocal"_L1, "not"_L1, "or"_L1, "pass"_L1, "raise"_L1,
    "return"_L1, "try"_L1, "while"_L1, "with"_L1, "yield"_L1
};
static const QLatin1StringView k_kw_qml[] = {
    "alias"_L1, "as"_L1, "break"_L1, "case"_L1, "catch"_L1, "component"_L1, "const"_L1,
    "continue"_L1, "default"_L1, "delete"_L1, "do"_L1, "else"_L1, "enum"_L1, "export"_L1,
    "extends"_L1, "false"_L1, "finally"_L1, "for"_L1, "function"_L1, "if"_L1, "import"_L1,
    "in"_L1, "instanceof"_L1, "let"_L1, "new"_L1, "null"_L1, "on"_L1, "pragma"_L1,
    "property"_L1, "readonly"_L1, "required"_L1, "return"_L1, "signal"_L1, "switch"_L1,
    "this"_L1, "throw"_L1, "true"_L1, "try"_L1, "typeof"_L1, "var"_L1, "void"_L1, "while"_L1,
    "with"_L1, "yield"_L1
};
static const QLatin1StringView k_ty_qml[] = {
    "alias"_L1, "bool"_L1, "color"_L1, "date"_L1, "double"_L1, "font"_L1, "int"_L1, "list"_L1,
    "matrix4x4"_L1, "point"_L1, "quaternion"_L1, "real"_L1, "rect"_L1, "size"_L1, "string"_L1,
    "url"_L1, "var"_L1, "variant"_L1, "vector2d"_L1, "vector3d"_L1, "vector4d"_L1
};
static const QLatin1StringView k_kw_ruby[] = {
    "alias"_L1, "and"_L1, "begin"_L1, "break"_L1, "case"_L1, "class"_L1, "def"_L1,
    "defined?"_L1, "do"_L1, "else"_L1, "elsif"_L1, "end"_L1, "ensure"_L1, "false"_L1, "for"_L1,
    "if"_L1, "in"_L1, "module"_L1, "next"_L1, "nil"_L1, "not"_L1, "or"_L1, "redo"_L1,
    "rescue"_L1, "retry"_L1, "return"_L1, "self"_L1, "super"_L1, "then"_L1, "true"_L1,
    "undef"_L1, "unless"_L1, "until"_L1, "when"_L1, "while"_L1, "yield"_L1
};
static const QLatin1StringView k_kw_rust[] = {
    "Self"_L1, "as"_L1, "async"_L1, "await"_L1, "break"_L1, "const"_L1, "continue"_L1,
    "crate"_L1, "dyn"_L1, "else"_L1, "enum"_L1, "extern"_L1, "false"_L1, "fn"_L1, "for"_L1,
    "if"_L1, "impl"_L1, "in"_L1, "let"_L1, "loop"_L1, "match"_L1, "mod"_L1, "move"_L1,
    "mut"_L1, "pub"_L1, "ref"_L1, "return"_L1, "self"_L1, "static"_L1, "struct"_L1, "super"_L1,
    "trait"_L1, "true"_L1, "type"_L1, "unsafe"_L1, "use"_L1, "where"_L1, "while"_L1
};
static const QLatin1StringView k_kw_shell[] = {
    "alias"_L1, "bg"_L1, "break"_L1, "builtin"_L1, "case"_L1, "cd"_L1, "continue"_L1,
    "declare"_L1, "do"_L1, "done"_L1, "echo"_L1, "elif"_L1, "else"_L1, "esac"_L1, "eval"_L1,
    "exec"_L1, "exit"_L1, "export"_L1, "fi"_L1, "for"_L1, "function"_L1, "getopts"_L1,
    "hash"_L1, "if"_L1, "in"_L1, "local"_L1, "printf"_L1, "pwd"_L1, "read"_L1, "readonly"_L1,
    "return"_L1, "select"_L1, "set"_L1, "shift"_L1, "source"_L1, "test"_L1, "then"_L1,
    "time"_L1, "trap"_L1, "type"_L1, "ulimit"_L1, "umask"_L1, "unalias"_L1, "unset"_L1,
    "until"_L1, "wait"_L1, "while"_L1
};
static const QLatin1StringView k_kw_sql[] = {
    "add"_L1, "all"_L1, "alter"_L1, "and"_L1, "any"_L1, "as"_L1, "asc"_L1, "between"_L1,
    "by"_L1, "case"_L1, "cast"_L1, "check"_L1, "column"_L1, "constraint"_L1, "create"_L1,
    "cross"_L1, "default"_L1, "delete"_L1, "desc"_L1, "distinct"_L1, "drop"_L1, "else"_L1,
    "end"_L1, "exists"_L1, "foreign"_L1, "from"_L1, "full"_L1, "group"_L1, "having"_L1,
    "if"_L1, "in"_L1, "index"_L1, "inner"_L1, "insert"_L1, "into"_L1, "is"_L1, "join"_L1,
    "key"_L1, "left"_L1, "like"_L1, "limit"_L1, "not"_L1, "null"_L1, "on"_L1, "or"_L1,
    "order"_L1, "outer"_L1, "primary"_L1, "references"_L1, "right"_L1, "select"_L1, "set"_L1,
    "table"_L1, "then"_L1, "union"_L1, "unique"_L1, "update"_L1, "values"_L1, "view"_L1,
    "when"_L1, "where"_L1, "with"_L1
};
static const QLatin1StringView k_kw_swift[] = {
    "Self"_L1, "actor"_L1, "any"_L1, "as"_L1, "associatedtype"_L1, "async"_L1, "await"_L1,
    "break"_L1, "case"_L1, "catch"_L1, "class"_L1, "continue"_L1, "default"_L1, "defer"_L1,
    "deinit"_L1, "do"_L1, "else"_L1, "enum"_L1, "extension"_L1, "fallthrough"_L1, "false"_L1,
    "fileprivate"_L1, "final"_L1, "for"_L1, "func"_L1, "guard"_L1, "if"_L1, "import"_L1,
    "in"_L1, "init"_L1, "inout"_L1, "internal"_L1, "is"_L1, "lazy"_L1, "let"_L1, "nil"_L1,
    "open"_L1, "operator"_L1, "private"_L1, "protocol"_L1, "public"_L1, "repeat"_L1,
    "rethrows"_L1, "return"_L1, "self"_L1, "static"_L1, "struct"_L1, "subscript"_L1,
    "super"_L1, "switch"_L1, "throw"_L1, "throws"_L1, "true"_L1, "try"_L1, "typealias"_L1,
    "var"_L1, "weak"_L1, "where"_L1, "while"_L1
};
static const QLatin1StringView k_kw_yaml[] = {
    "false"_L1, "no"_L1, "null"_L1, "off"_L1, "on"_L1, "true"_L1, "yes"_L1
};

static const QLatin1StringView k_ty_cpp[] = {
    "QByteArray"_L1, "QColor"_L1, "QDir"_L1, "QFile"_L1, "QHash"_L1, "QImage"_L1, "QList"_L1,
    "QMap"_L1, "QObject"_L1, "QPixmap"_L1, "QPointF"_L1, "QRectF"_L1, "QSet"_L1, "QSizeF"_L1,
    "QString"_L1, "QStringView"_L1, "QTimer"_L1, "QUrl"_L1, "QVariant"_L1, "QVector"_L1,
    "bool"_L1, "char"_L1, "char16_t"_L1, "char32_t"_L1, "char8_t"_L1, "double"_L1, "float"_L1,
    "int"_L1, "int16_t"_L1, "int32_t"_L1, "int64_t"_L1, "int8_t"_L1, "long"_L1, "map"_L1,
    "pair"_L1, "ptrdiff_t"_L1, "qint16"_L1, "qint32"_L1, "qint64"_L1, "qint8"_L1, "qreal"_L1,
    "qsizetype"_L1, "quint16"_L1, "quint32"_L1, "quint64"_L1, "quint8"_L1, "set"_L1,
    "shared_ptr"_L1, "short"_L1, "signed"_L1, "size_t"_L1, "ssize_t"_L1, "std"_L1, "string"_L1,
    "uint16_t"_L1, "uint32_t"_L1, "uint64_t"_L1, "uint8_t"_L1, "unique_ptr"_L1, "unsigned"_L1,
    "vector"_L1, "void"_L1, "wchar_t"_L1
};
static const QLatin1StringView k_ty_csharp[] = {
    "Boolean"_L1, "Byte"_L1, "Char"_L1, "Decimal"_L1, "Dictionary"_L1, "Double"_L1,
    "HashSet"_L1, "Int16"_L1, "Int32"_L1, "Int64"_L1, "List"_L1, "Object"_L1, "Single"_L1,
    "String"_L1, "Task"_L1, "UInt16"_L1, "UInt32"_L1, "UInt64"_L1, "bool"_L1, "byte"_L1,
    "char"_L1, "decimal"_L1, "double"_L1, "float"_L1, "int"_L1, "long"_L1, "object"_L1,
    "sbyte"_L1, "short"_L1, "string"_L1, "uint"_L1, "ulong"_L1, "ushort"_L1, "void"_L1
};
static const QLatin1StringView k_ty_go[] = {
    "any"_L1, "bool"_L1, "byte"_L1, "complex128"_L1, "complex64"_L1, "error"_L1, "float32"_L1,
    "float64"_L1, "int"_L1, "int16"_L1, "int32"_L1, "int64"_L1, "int8"_L1, "rune"_L1,
    "string"_L1, "uint"_L1, "uint16"_L1, "uint32"_L1, "uint64"_L1, "uint8"_L1, "uintptr"_L1
};
static const QLatin1StringView k_ty_java[] = {
    "ArrayList"_L1, "Boolean"_L1, "Byte"_L1, "Character"_L1, "Double"_L1, "Float"_L1,
    "HashMap"_L1, "HashSet"_L1, "Integer"_L1, "List"_L1, "Long"_L1, "Map"_L1, "Object"_L1,
    "Set"_L1, "Short"_L1, "String"_L1, "StringBuilder"_L1, "boolean"_L1, "byte"_L1, "char"_L1,
    "double"_L1, "float"_L1, "int"_L1, "long"_L1, "short"_L1, "void"_L1
};
static const QLatin1StringView k_ty_js[] = {
    "Array"_L1, "BigInt"_L1, "Boolean"_L1, "Date"_L1, "Error"_L1, "Function"_L1, "JSON"_L1,
    "Map"_L1, "Math"_L1, "Number"_L1, "Object"_L1, "Promise"_L1, "Proxy"_L1, "RegExp"_L1,
    "Set"_L1, "String"_L1, "Symbol"_L1, "WeakMap"_L1, "WeakSet"_L1
};
static const QLatin1StringView k_ty_kotlin[] = {
    "Any"_L1, "Array"_L1, "Boolean"_L1, "Byte"_L1, "Char"_L1, "CharSequence"_L1, "Double"_L1,
    "Float"_L1, "Int"_L1, "List"_L1, "Long"_L1, "Map"_L1, "MutableList"_L1, "MutableMap"_L1,
    "MutableSet"_L1, "Nothing"_L1, "Number"_L1, "Set"_L1, "Short"_L1, "String"_L1, "Unit"_L1
};
static const QLatin1StringView k_ty_lua[] = {
    "boolean"_L1, "number"_L1, "string"_L1, "table"_L1, "thread"_L1,
    "userdata"_L1
};
static const QLatin1StringView k_ty_php[] = {
    "array"_L1, "bool"_L1, "callable"_L1, "float"_L1, "int"_L1, "iterable"_L1,
    "mixed"_L1, "never"_L1, "object"_L1, "parent"_L1, "self"_L1,
    "string"_L1, "void"_L1
};
static const QLatin1StringView k_ty_python[] = {
    "Any"_L1, "Callable"_L1, "Dict"_L1, "Iterable"_L1, "Iterator"_L1, "List"_L1, "Optional"_L1,
    "Sequence"_L1, "Set"_L1, "Tuple"_L1, "Union"_L1, "bool"_L1, "bytearray"_L1, "bytes"_L1,
    "complex"_L1, "dict"_L1, "float"_L1, "frozenset"_L1, "int"_L1, "list"_L1, "object"_L1,
    "set"_L1, "str"_L1, "tuple"_L1, "type"_L1
};
static const QLatin1StringView k_ty_ruby[] = {
    "Array"_L1, "Comparable"_L1, "Enumerable"_L1, "Exception"_L1, "Float"_L1, "Hash"_L1,
    "Integer"_L1, "Numeric"_L1, "Object"_L1, "Proc"_L1, "Range"_L1, "Regexp"_L1, "String"_L1,
    "Struct"_L1, "Symbol"_L1, "Time"_L1
};
static const QLatin1StringView k_ty_rust[] = {
    "Arc"_L1, "Box"_L1, "HashMap"_L1, "HashSet"_L1, "Option"_L1, "Rc"_L1, "Result"_L1,
    "String"_L1, "Vec"_L1, "bool"_L1, "char"_L1, "f32"_L1, "f64"_L1, "i128"_L1, "i16"_L1,
    "i32"_L1, "i64"_L1, "i8"_L1, "isize"_L1, "str"_L1, "u128"_L1, "u16"_L1, "u32"_L1, "u64"_L1,
    "u8"_L1, "usize"_L1
};
static const QLatin1StringView k_ty_swift[] = {
    "Any"_L1, "AnyObject"_L1, "Array"_L1, "Bool"_L1, "Character"_L1, "Dictionary"_L1,
    "Double"_L1, "Float"_L1, "Int"_L1, "Int16"_L1, "Int32"_L1, "Int64"_L1, "Int8"_L1,
    "Optional"_L1, "Set"_L1, "String"_L1, "UInt"_L1, "UInt16"_L1, "UInt32"_L1, "UInt64"_L1,
    "UInt8"_L1, "Void"_L1
};

template <int N>
constexpr WordList wl(const QLatin1StringView (&a)[N]) { return WordList{ a, N }; }

// Designierte Initialisierer, damit eine Sprache mit dreizehn Feldern lesbar bleibt.
// GCC meldet dafuer -Wmissing-field-initializers, obwohl alle Felder Vorgaben haben
// (C kennt die Ausnahme, C++ nicht) - rund 40 Warnungen fuer nichts, daher aus.
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

// Wortpaare fuer `FoldKind::Keywords`
//  Sortiert wie alle Wortlisten (Binaersuche). `else`/`elseif` stehen in KEINER
//  der beiden Listen: sie oeffnen nichts und schliessen nichts.
constexpr QLatin1StringView k_fo_lua[]   = { "do"_L1, "for"_L1, "function"_L1,
                                             "if"_L1, "repeat"_L1, "while"_L1 };
constexpr QLatin1StringView k_fc_lua[]   = { "end"_L1, "until"_L1 };
constexpr QLatin1StringView k_fo_ruby[]  = { "begin"_L1, "case"_L1, "class"_L1, "def"_L1,
                                             "do"_L1, "module"_L1, "unless"_L1, "while"_L1 };
constexpr QLatin1StringView k_fc_ruby[]  = { "end"_L1 };
constexpr QLatin1StringView k_fo_cmake[] = { "foreach"_L1, "function"_L1, "if"_L1,
                                             "macro"_L1, "while"_L1 };
constexpr QLatin1StringView k_fc_cmake[] = { "endforeach"_L1, "endfunction"_L1,
                                             "endif"_L1, "endmacro"_L1, "endwhile"_L1 };

const LanguageDef s_plain {
    .id = "text"_L1, .label = "Text"_L1, .kind = ScannerKind::PlainText
};

const LanguageDef s_langs[] = {
    { .id = "cpp"_L1, .label = "C++"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_cpp), .types = wl(k_ty_cpp),
      .lineComment = "//"_L1, .blockOpen = "/*"_L1, .blockClose = "*/"_L1,
      .preprocHash = true, .rawStrings = true , .fold = FoldKind::Braces },

    { .id = "python"_L1, .label = "Python"_L1, .kind = ScannerKind::Script,
      .keywords = wl(k_kw_python), .types = wl(k_ty_python),
      .lineComment = "#"_L1, .tripleQuotes = true , .fold = FoldKind::Indent },

    { .id = "markdown"_L1, .label = "Markdown"_L1, .kind = ScannerKind::Markdown , .fold = FoldKind::Headings },

    { .id = "java"_L1, .label = "Java"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_java), .types = wl(k_ty_java),
      .lineComment = "//"_L1, .blockOpen = "/*"_L1, .blockClose = "*/"_L1 , .fold = FoldKind::Braces },

    { .id = "js"_L1, .label = "JavaScript / TypeScript"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_js), .types = wl(k_ty_js),
      .lineComment = "//"_L1, .blockOpen = "/*"_L1, .blockClose = "*/"_L1,
      .templateStrings = true, .fold = FoldKind::Braces },

    { .id = "csharp"_L1, .label = "C#"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_csharp), .types = wl(k_ty_csharp),
      .lineComment = "//"_L1, .blockOpen = "/*"_L1, .blockClose = "*/"_L1 , .fold = FoldKind::Braces },

    { .id = "go"_L1, .label = "Go"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_go), .types = wl(k_ty_go),
      .lineComment = "//"_L1, .blockOpen = "/*"_L1, .blockClose = "*/"_L1 , .fold = FoldKind::Braces },

    { .id = "rust"_L1, .label = "Rust"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_rust), .types = wl(k_ty_rust),
      .lineComment = "//"_L1, .blockOpen = "/*"_L1, .blockClose = "*/"_L1 , .fold = FoldKind::Braces },

    { .id = "php"_L1, .label = "PHP"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_php), .types = wl(k_ty_php),
      .lineComment = "//"_L1, .lineComment2 = "#"_L1,
      .blockOpen = "/*"_L1, .blockClose = "*/"_L1 , .fold = FoldKind::Braces },

    { .id = "swift"_L1, .label = "Swift"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_swift), .types = wl(k_ty_swift),
      .lineComment = "//"_L1, .blockOpen = "/*"_L1, .blockClose = "*/"_L1 , .fold = FoldKind::Braces },

    { .id = "kotlin"_L1, .label = "Kotlin"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_kotlin), .types = wl(k_ty_kotlin),
      .lineComment = "//"_L1, .blockOpen = "/*"_L1, .blockClose = "*/"_L1 , .fold = FoldKind::Braces },

    { .id = "shell"_L1, .label = "Shell"_L1, .kind = ScannerKind::Script,
      .keywords = wl(k_kw_shell), .lineComment = "#"_L1 , .fold = FoldKind::Braces },

    { .id = "ruby"_L1, .label = "Ruby"_L1, .kind = ScannerKind::Script,
      .keywords = wl(k_kw_ruby), .types = wl(k_ty_ruby), .lineComment = "#"_L1 , .fold = FoldKind::Keywords, .foldOpen = wl(k_fo_ruby), .foldClose = wl(k_fc_ruby) },

    { .id = "dart"_L1, .label = "Dart"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_dart), .types = wl(k_ty_dart),
      .lineComment = "//"_L1, .blockOpen = "/*"_L1, .blockClose = "*/"_L1,
      .fold = FoldKind::Braces },

    { .id = "perl"_L1, .label = "Perl"_L1, .kind = ScannerKind::Script,
      .keywords = wl(k_kw_perl),
      .lineComment = "#"_L1,
      .fold = FoldKind::Braces },

    { .id = "r"_L1, .label = "R"_L1, .kind = ScannerKind::Script,
      .keywords = wl(k_kw_r), .types = wl(k_ty_r),
      .lineComment = "#"_L1,
      .fold = FoldKind::Braces },

    { .id = "lua"_L1, .label = "Lua"_L1, .kind = ScannerKind::Script,
      .keywords = wl(k_kw_lua), .types = wl(k_ty_lua),
      .lineComment = "--"_L1, .blockOpen = "--[["_L1, .blockClose = "]]"_L1 , .fold = FoldKind::Keywords, .foldOpen = wl(k_fo_lua), .foldClose = wl(k_fc_lua) },

    { .id = "cmake"_L1, .label = "CMake"_L1, .kind = ScannerKind::Script,
      .keywords = wl(k_kw_cmake), .lineComment = "#"_L1, .caseSensitive = false , .fold = FoldKind::Keywords, .foldOpen = wl(k_fo_cmake), .foldClose = wl(k_fc_cmake) },

    { .id = "yaml"_L1, .label = "YAML"_L1, .kind = ScannerKind::Script,
      .keywords = wl(k_kw_yaml), .lineComment = "#"_L1 , .fold = FoldKind::Indent },

    { .id = "sql"_L1, .label = "SQL"_L1, .kind = ScannerKind::Script,
      .keywords = wl(k_kw_sql), .lineComment = "--"_L1,
      .blockOpen = "/*"_L1, .blockClose = "*/"_L1, .caseSensitive = false },

    { .id = "xml"_L1, .label = "XML / HTML"_L1, .kind = ScannerKind::Markup,
      .blockOpen = "<!--"_L1, .blockClose = "-->"_L1,
      .fold = FoldKind::Tags },

    { .id = "css"_L1, .label = "CSS"_L1, .kind = ScannerKind::CLike,
      .blockOpen = "/*"_L1, .blockClose = "*/"_L1,
      .hashColors = true, .propertyColon = ColonStyle::Anywhere , .fold = FoldKind::Braces },

    { .id = "json"_L1, .label = "JSON"_L1, .kind = ScannerKind::CLike , .fold = FoldKind::Braces },

    { .id = "qml"_L1, .label = "QML"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_qml), .types = wl(k_ty_qml),
      .lineComment = "//"_L1, .blockOpen = "/*"_L1, .blockClose = "*/"_L1,
      .propertyColon = ColonStyle::LineStart, .typeBeforeBrace = true,
      .templateStrings = true, .fold = FoldKind::Braces },

    { .id = "ini"_L1, .label = "INI"_L1, .kind = ScannerKind::Ini,
      .lineComment = "#"_L1, .lineComment2 = ";"_L1 , .fold = FoldKind::Sections },

    { .id = "toml"_L1, .label = "TOML"_L1, .kind = ScannerKind::Ini,
      .lineComment = "#"_L1 , .fold = FoldKind::Sections },

    { .id = "supp"_L1, .label = "Suppressions"_L1, .kind = ScannerKind::Script,
      .lineComment = "#"_L1 , .fold = FoldKind::Braces },
};

#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

const LanguageDef* findById(QLatin1StringView id) {
    for (const LanguageDef& d : s_langs)
        if (d.id == id) return &d;
    return nullptr;
}
const LanguageDef* findById(QStringView id) {
    for (const LanguageDef& d : s_langs)
        if (id.compare(d.id) == 0) return &d;
    return nullptr;
}

// Endung -> Sprach-Bezeichner, bewusst eine Tabelle und keine if-Kette: eine neue Endung ist eine Zeile.
// `.h` zählt als C/C++ - Objective-C zu raten wäre schlimmer als eine feste Wahl.
struct ExtEntry { QLatin1StringView ext; QLatin1StringView lang; };

const ExtEntry s_byExtension[] = {
    { "c"_L1, "cpp"_L1 },      { "cc"_L1, "cpp"_L1 },    { "cpp"_L1, "cpp"_L1 },
    { "cxx"_L1, "cpp"_L1 },    { "h"_L1, "cpp"_L1 },     { "hpp"_L1, "cpp"_L1 },
    { "hxx"_L1, "cpp"_L1 },
    { "py"_L1, "python"_L1 },
    { "md"_L1, "markdown"_L1 },
    { "java"_L1, "java"_L1 },
    { "js"_L1, "js"_L1 },      { "jsx"_L1, "js"_L1 },    { "ts"_L1, "js"_L1 },
    { "tsx"_L1, "js"_L1 },
    { "cs"_L1, "csharp"_L1 },
    { "go"_L1, "go"_L1 },
    { "rs"_L1, "rust"_L1 },
    { "php"_L1, "php"_L1 },
    { "swift"_L1, "swift"_L1 },
    { "kt"_L1, "kotlin"_L1 },
    { "sh"_L1, "shell"_L1 },   { "bash"_L1, "shell"_L1 }, { "zsh"_L1, "shell"_L1 },
    { "rb"_L1, "ruby"_L1 },
    { "lua"_L1, "lua"_L1 },
    { "dart"_L1, "dart"_L1 },
    { "pl"_L1, "perl"_L1 },
    { "pm"_L1, "perl"_L1 },
    { "r"_L1, "r"_L1 },
    { "cmake"_L1, "cmake"_L1 },
    { "yaml"_L1, "yaml"_L1 },  { "yml"_L1, "yaml"_L1 },
    { "sql"_L1, "sql"_L1 },
    { "xml"_L1, "xml"_L1 },    { "html"_L1, "xml"_L1 },  { "htm"_L1, "xml"_L1 },
    { "css"_L1, "css"_L1 },    { "scss"_L1, "css"_L1 },  { "less"_L1, "css"_L1 },
    { "json"_L1, "json"_L1 },
    { "qml"_L1, "qml"_L1 },
    { "supp"_L1, "supp"_L1 },
    { "qrc"_L1, "xml"_L1 },
    { "pro"_L1, "ini"_L1 },    { "pri"_L1, "ini"_L1 },
    { "ini"_L1, "ini"_L1 },    { "cfg"_L1, "ini"_L1 },   { "conf"_L1, "ini"_L1 },
    { "toml"_L1, "ini"_L1 },
};

const ExtEntry s_byBaseName[] = {
    { "cmakelists"_L1, "cmake"_L1 },
    { "makefile"_L1,   "shell"_L1 },
    { "dockerfile"_L1, "shell"_L1 },
};

}  // namespace

QStringList knownExtensions() {
    QStringList raus;
    raus.reserve(int(std::size(s_byExtension)));
    for (const ExtEntry& e : s_byExtension) raus.append(QString(e.ext));
    return raus;
}

bool containsWord(const WordList& list, QStringView word) {
    if (list.count == 0 || word.isEmpty()) return false;
    const auto* ende = list.words + list.count;
    const auto* treffer = std::lower_bound(
        list.words, ende, word,
        [](QLatin1StringView a, QStringView b) { return a.compare(b) < 0; });
    return treffer != ende && treffer->compare(word) == 0;
}

const LanguageDef& plainTextLanguage() { return s_plain; }

const LanguageDef& languageForId(QStringView id) {
    const LanguageDef* d = findById(id);
    return d ? *d : s_plain;
}

const LanguageDef& languageForPath(QStringView path) {
    if (path.isEmpty()) return s_plain;

    qsizetype trenner = path.lastIndexOf(u'/');
#ifdef Q_OS_WIN
    trenner = qMax(trenner, path.lastIndexOf(u'\\'));
#endif
    const QStringView name = path.mid(trenner + 1);
    if (name.isEmpty()) return s_plain;

    const qsizetype punkt = name.lastIndexOf(u'.');
    if (punkt > 0) {
        const QString endung = name.mid(punkt + 1).toString().toLower();
        for (const ExtEntry& e : s_byExtension)
            if (endung.compare(e.ext) == 0) {
                const LanguageDef* d = findById(e.lang);
                return d ? *d : s_plain;
            }
    }

    //  Endungslos oder unbekannte Endung: ueber den Namensanfang versuchen
    //  (Makefile, CMakeLists.txt, Dockerfile).
    const QString basis = (punkt > 0 ? name.left(punkt) : name).toString().toLower();
    for (const ExtEntry& e : s_byBaseName)
        if (basis.compare(e.ext) == 0) {
            const LanguageDef* d = findById(e.lang);
            return d ? *d : s_plain;
        }

    return s_plain;
}

}  // namespace mg::editor
