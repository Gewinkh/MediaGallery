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

//  Assembly: Befehle und Direktiven zusammen, Register als Typen. Die Namen
//  stehen ohne den fuehrenden Punkt - der Zerleger trennt `.text` in Punkt und
//  Wort. x86-64 und aarch64 teilen sich die Liste; welcher Satz gemeint ist,
//  sagt die Datei, nicht die Endung.
static const QLatin1StringView k_kw_asm[] = {
    "adc"_L1, "add"_L1, "addps"_L1, "adds"_L1, "addsd"_L1, "addv"_L1, "adr"_L1, "adrp"_L1,
    "align"_L1, "and"_L1, "ands"_L1, "arch"_L1, "arm"_L1, "ascii"_L1, "asciz"_L1, "asr"_L1,
    "att_syntax"_L1, "b"_L1, "balign"_L1, "bic"_L1, "blr"_L1, "br"_L1, "brk"_L1, "bsf"_L1, "bsr"_L1,
    "bss"_L1, "bswap"_L1, "bt"_L1, "btc"_L1, "btr"_L1, "bts"_L1, "byte"_L1, "call"_L1, "cbnz"_L1,
    "cbw"_L1, "cbz"_L1, "cdq"_L1, "cdqe"_L1, "cfi_def_cfa"_L1, "cfi_endproc"_L1, "cfi_offset"_L1,
    "cfi_startproc"_L1, "clc"_L1, "cld"_L1, "clz"_L1, "cmc"_L1, "cmn"_L1, "cmova"_L1, "cmovae"_L1,
    "cmovb"_L1, "cmovbe"_L1, "cmove"_L1, "cmovg"_L1, "cmovge"_L1, "cmovl"_L1, "cmovle"_L1,
    "cmovne"_L1, "cmovnz"_L1, "cmovz"_L1, "cmp"_L1, "cmpxchg"_L1, "cnt"_L1, "code16"_L1,
    "code32"_L1, "code64"_L1, "comm"_L1, "cpu"_L1, "cqo"_L1, "csel"_L1, "cset"_L1, "csinc"_L1,
    "cvtsi2sd"_L1, "cvttsd2si"_L1, "cwd"_L1, "cwde"_L1, "data"_L1, "dec"_L1, "div"_L1, "divps"_L1,
    "double"_L1, "dup"_L1, "else"_L1, "elseif"_L1, "endbr64"_L1, "endfunc"_L1, "endif"_L1,
    "endm"_L1, "endr"_L1, "enter"_L1, "eor"_L1, "equ"_L1, "err"_L1, "error"_L1, "extern"_L1,
    "file"_L1, "fill"_L1, "float"_L1, "fmov"_L1, "func"_L1, "global"_L1, "globl"_L1, "hidden"_L1,
    "hlt"_L1, "ident"_L1, "idiv"_L1, "if"_L1, "imul"_L1, "inc"_L1, "incbin"_L1, "include"_L1,
    "int"_L1, "intel_syntax"_L1, "irp"_L1, "ja"_L1, "jae"_L1, "jb"_L1, "jbe"_L1, "jc"_L1, "je"_L1,
    "jg"_L1, "jge"_L1, "jl"_L1, "jle"_L1, "jmp"_L1, "jnc"_L1, "jne"_L1, "jno"_L1, "jnp"_L1,
    "jns"_L1, "jnz"_L1, "jo"_L1, "jp"_L1, "js"_L1, "jz"_L1, "ldp"_L1, "ldr"_L1, "ldrb"_L1,
    "ldrh"_L1, "lea"_L1, "leave"_L1, "lfence"_L1, "loc"_L1, "local"_L1, "lock"_L1, "lodsb"_L1,
    "lodsq"_L1, "long"_L1, "loop"_L1, "lsl"_L1, "lsr"_L1, "lzcnt"_L1, "macro"_L1, "madd"_L1,
    "mfence"_L1, "mov"_L1, "movaps"_L1, "movd"_L1, "movdqa"_L1, "movdqu"_L1, "movk"_L1, "movn"_L1,
    "movq"_L1, "movsb"_L1, "movsd"_L1, "movsq"_L1, "movsw"_L1, "movsx"_L1, "movsxd"_L1, "movups"_L1,
    "movz"_L1, "movzx"_L1, "msub"_L1, "mul"_L1, "mulps"_L1, "mulsd"_L1, "neg"_L1, "nop"_L1,
    "noprefix"_L1, "not"_L1, "octa"_L1, "or"_L1, "org"_L1, "orr"_L1, "p2align"_L1, "packsswb"_L1,
    "packuswb"_L1, "paddb"_L1, "pand"_L1, "pandn"_L1, "pause"_L1, "pcmpeqb"_L1, "pcmpeqd"_L1,
    "pcmpeqw"_L1, "pmovmskb"_L1, "pop"_L1, "popcnt"_L1, "popf"_L1, "popsection"_L1, "por"_L1,
    "prefetcht0"_L1, "prefix"_L1, "previous"_L1, "pshufb"_L1, "pshufd"_L1, "pslld"_L1, "psllw"_L1,
    "psrld"_L1, "psrlw"_L1, "psubb"_L1, "punpckhbw"_L1, "punpcklbw"_L1, "push"_L1, "pushf"_L1,
    "pushsection"_L1, "pxor"_L1, "quad"_L1, "rbit"_L1, "rcl"_L1, "rcr"_L1, "rep"_L1, "repe"_L1,
    "repne"_L1, "repnz"_L1, "rept"_L1, "ret"_L1, "rodata"_L1, "rol"_L1, "ror"_L1, "sal"_L1,
    "sar"_L1, "sbb"_L1, "scasb"_L1, "sdiv"_L1, "section"_L1, "set"_L1, "seta"_L1, "setae"_L1,
    "setb"_L1, "setbe"_L1, "sete"_L1, "setg"_L1, "setge"_L1, "setl"_L1, "setle"_L1, "setne"_L1,
    "setns"_L1, "setnz"_L1, "sets"_L1, "setz"_L1, "sfence"_L1, "shl"_L1, "short"_L1, "shr"_L1,
    "shrn"_L1, "single"_L1, "size"_L1, "skip"_L1, "space"_L1, "sqrtps"_L1, "stc"_L1, "std"_L1,
    "stosb"_L1, "stosd"_L1, "stosq"_L1, "stosw"_L1, "stp"_L1, "str"_L1, "strb"_L1, "strh"_L1,
    "string"_L1, "sub"_L1, "subps"_L1, "subs"_L1, "svc"_L1, "syscall"_L1, "sysret"_L1, "tbnz"_L1,
    "tbz"_L1, "test"_L1, "text"_L1, "thumb"_L1, "type"_L1, "tzcnt"_L1, "ucomisd"_L1, "udiv"_L1,
    "umov"_L1, "uzp1"_L1, "uzp2"_L1, "vaddps"_L1, "vextracti128"_L1, "vinserti128"_L1, "vmovaps"_L1,
    "vmovd"_L1, "vmovdqa"_L1, "vmovdqu"_L1, "vmovq"_L1, "vmovups"_L1, "vmulps"_L1, "vpackuswb"_L1,
    "vpaddb"_L1, "vpand"_L1, "vpandn"_L1, "vpbroadcastb"_L1, "vpbroadcastd"_L1, "vpbroadcastw"_L1,
    "vpcmpeqb"_L1, "vpcmpeqd"_L1, "vpcmpeqw"_L1, "vperm2i128"_L1, "vpmovmskb"_L1, "vpor"_L1,
    "vpshufb"_L1, "vpsubb"_L1, "vpxor"_L1, "vsubps"_L1, "vzeroall"_L1, "vzeroupper"_L1, "weak"_L1,
    "word"_L1, "xadd"_L1, "xchg"_L1, "xor"_L1, "zero"_L1
};
static const QLatin1StringView k_ty_asm[] = {
    "ah"_L1, "al"_L1, "ax"_L1, "b0"_L1, "b1"_L1, "b10"_L1, "b11"_L1, "b12"_L1, "b13"_L1, "b14"_L1,
    "b15"_L1, "b16"_L1, "b17"_L1, "b18"_L1, "b19"_L1, "b2"_L1, "b20"_L1, "b21"_L1, "b22"_L1,
    "b23"_L1, "b24"_L1, "b25"_L1, "b26"_L1, "b27"_L1, "b28"_L1, "b29"_L1, "b3"_L1, "b30"_L1,
    "b31"_L1, "b4"_L1, "b5"_L1, "b6"_L1, "b7"_L1, "b8"_L1, "b9"_L1, "bh"_L1, "bl"_L1, "bp"_L1,
    "bpl"_L1, "bx"_L1, "ch"_L1, "cl"_L1, "cx"_L1, "d0"_L1, "d1"_L1, "d10"_L1, "d11"_L1, "d12"_L1,
    "d13"_L1, "d14"_L1, "d15"_L1, "d16"_L1, "d17"_L1, "d18"_L1, "d19"_L1, "d2"_L1, "d20"_L1,
    "d21"_L1, "d22"_L1, "d23"_L1, "d24"_L1, "d25"_L1, "d26"_L1, "d27"_L1, "d28"_L1, "d29"_L1,
    "d3"_L1, "d30"_L1, "d31"_L1, "d4"_L1, "d5"_L1, "d6"_L1, "d7"_L1, "d8"_L1, "d9"_L1, "dh"_L1,
    "di"_L1, "dil"_L1, "dl"_L1, "dx"_L1, "eax"_L1, "ebp"_L1, "ebx"_L1, "ecx"_L1, "edi"_L1, "edx"_L1,
    "eflags"_L1, "esi"_L1, "esp"_L1, "fp"_L1, "h0"_L1, "h1"_L1, "h10"_L1, "h11"_L1, "h12"_L1,
    "h13"_L1, "h14"_L1, "h15"_L1, "h16"_L1, "h17"_L1, "h18"_L1, "h19"_L1, "h2"_L1, "h20"_L1,
    "h21"_L1, "h22"_L1, "h23"_L1, "h24"_L1, "h25"_L1, "h26"_L1, "h27"_L1, "h28"_L1, "h29"_L1,
    "h3"_L1, "h30"_L1, "h31"_L1, "h4"_L1, "h5"_L1, "h6"_L1, "h7"_L1, "h8"_L1, "h9"_L1, "lr"_L1,
    "nzcv"_L1, "pc"_L1, "q0"_L1, "q1"_L1, "q10"_L1, "q11"_L1, "q12"_L1, "q13"_L1, "q14"_L1,
    "q15"_L1, "q16"_L1, "q17"_L1, "q18"_L1, "q19"_L1, "q2"_L1, "q20"_L1, "q21"_L1, "q22"_L1,
    "q23"_L1, "q24"_L1, "q25"_L1, "q26"_L1, "q27"_L1, "q28"_L1, "q29"_L1, "q3"_L1, "q30"_L1,
    "q31"_L1, "q4"_L1, "q5"_L1, "q6"_L1, "q7"_L1, "q8"_L1, "q9"_L1, "r0"_L1, "r0b"_L1, "r0d"_L1,
    "r0w"_L1, "r1"_L1, "r10"_L1, "r10b"_L1, "r10d"_L1, "r10w"_L1, "r11"_L1, "r11b"_L1, "r11d"_L1,
    "r11w"_L1, "r12"_L1, "r12b"_L1, "r12d"_L1, "r12w"_L1, "r13"_L1, "r13b"_L1, "r13d"_L1, "r13w"_L1,
    "r14"_L1, "r14b"_L1, "r14d"_L1, "r14w"_L1, "r15"_L1, "r15b"_L1, "r15d"_L1, "r15w"_L1, "r1b"_L1,
    "r1d"_L1, "r1w"_L1, "r2"_L1, "r2b"_L1, "r2d"_L1, "r2w"_L1, "r3"_L1, "r3b"_L1, "r3d"_L1,
    "r3w"_L1, "r4"_L1, "r4b"_L1, "r4d"_L1, "r4w"_L1, "r5"_L1, "r5b"_L1, "r5d"_L1, "r5w"_L1, "r6"_L1,
    "r6b"_L1, "r6d"_L1, "r6w"_L1, "r7"_L1, "r7b"_L1, "r7d"_L1, "r7w"_L1, "r8"_L1, "r8b"_L1,
    "r8d"_L1, "r8w"_L1, "r9"_L1, "r9b"_L1, "r9d"_L1, "r9w"_L1, "rax"_L1, "rbp"_L1, "rbx"_L1,
    "rcx"_L1, "rdi"_L1, "rdx"_L1, "rflags"_L1, "rip"_L1, "rsi"_L1, "rsp"_L1, "s0"_L1, "s1"_L1,
    "s10"_L1, "s11"_L1, "s12"_L1, "s13"_L1, "s14"_L1, "s15"_L1, "s16"_L1, "s17"_L1, "s18"_L1,
    "s19"_L1, "s2"_L1, "s20"_L1, "s21"_L1, "s22"_L1, "s23"_L1, "s24"_L1, "s25"_L1, "s26"_L1,
    "s27"_L1, "s28"_L1, "s29"_L1, "s3"_L1, "s30"_L1, "s31"_L1, "s4"_L1, "s5"_L1, "s6"_L1, "s7"_L1,
    "s8"_L1, "s9"_L1, "si"_L1, "sil"_L1, "sp"_L1, "spl"_L1, "v0"_L1, "v1"_L1, "v10"_L1, "v11"_L1,
    "v12"_L1, "v13"_L1, "v14"_L1, "v15"_L1, "v16"_L1, "v17"_L1, "v18"_L1, "v19"_L1, "v2"_L1,
    "v20"_L1, "v21"_L1, "v22"_L1, "v23"_L1, "v24"_L1, "v25"_L1, "v26"_L1, "v27"_L1, "v28"_L1,
    "v29"_L1, "v3"_L1, "v30"_L1, "v31"_L1, "v4"_L1, "v5"_L1, "v6"_L1, "v7"_L1, "v8"_L1, "v9"_L1,
    "w0"_L1, "w1"_L1, "w10"_L1, "w11"_L1, "w12"_L1, "w13"_L1, "w14"_L1, "w15"_L1, "w16"_L1,
    "w17"_L1, "w18"_L1, "w19"_L1, "w2"_L1, "w20"_L1, "w21"_L1, "w22"_L1, "w23"_L1, "w24"_L1,
    "w25"_L1, "w26"_L1, "w27"_L1, "w28"_L1, "w29"_L1, "w3"_L1, "w30"_L1, "w31"_L1, "w4"_L1, "w5"_L1,
    "w6"_L1, "w7"_L1, "w8"_L1, "w9"_L1, "wzr"_L1, "x0"_L1, "x1"_L1, "x10"_L1, "x11"_L1, "x12"_L1,
    "x13"_L1, "x14"_L1, "x15"_L1, "x16"_L1, "x17"_L1, "x18"_L1, "x19"_L1, "x2"_L1, "x20"_L1,
    "x21"_L1, "x22"_L1, "x23"_L1, "x24"_L1, "x25"_L1, "x26"_L1, "x27"_L1, "x28"_L1, "x29"_L1,
    "x3"_L1, "x30"_L1, "x31"_L1, "x4"_L1, "x5"_L1, "x6"_L1, "x7"_L1, "x8"_L1, "x9"_L1, "xmm0"_L1,
    "xmm1"_L1, "xmm10"_L1, "xmm11"_L1, "xmm12"_L1, "xmm13"_L1, "xmm14"_L1, "xmm15"_L1, "xmm16"_L1,
    "xmm17"_L1, "xmm18"_L1, "xmm19"_L1, "xmm2"_L1, "xmm20"_L1, "xmm21"_L1, "xmm22"_L1, "xmm23"_L1,
    "xmm24"_L1, "xmm25"_L1, "xmm26"_L1, "xmm27"_L1, "xmm28"_L1, "xmm29"_L1, "xmm3"_L1, "xmm30"_L1,
    "xmm31"_L1, "xmm4"_L1, "xmm5"_L1, "xmm6"_L1, "xmm7"_L1, "xmm8"_L1, "xmm9"_L1, "xzr"_L1,
    "ymm0"_L1, "ymm1"_L1, "ymm10"_L1, "ymm11"_L1, "ymm12"_L1, "ymm13"_L1, "ymm14"_L1, "ymm15"_L1,
    "ymm16"_L1, "ymm17"_L1, "ymm18"_L1, "ymm19"_L1, "ymm2"_L1, "ymm20"_L1, "ymm21"_L1, "ymm22"_L1,
    "ymm23"_L1, "ymm24"_L1, "ymm25"_L1, "ymm26"_L1, "ymm27"_L1, "ymm28"_L1, "ymm29"_L1, "ymm3"_L1,
    "ymm30"_L1, "ymm31"_L1, "ymm4"_L1, "ymm5"_L1, "ymm6"_L1, "ymm7"_L1, "ymm8"_L1, "ymm9"_L1,
    "zmm0"_L1, "zmm1"_L1, "zmm10"_L1, "zmm11"_L1, "zmm12"_L1, "zmm13"_L1, "zmm14"_L1, "zmm15"_L1,
    "zmm16"_L1, "zmm17"_L1, "zmm18"_L1, "zmm19"_L1, "zmm2"_L1, "zmm20"_L1, "zmm21"_L1, "zmm22"_L1,
    "zmm23"_L1, "zmm24"_L1, "zmm25"_L1, "zmm26"_L1, "zmm27"_L1, "zmm28"_L1, "zmm29"_L1, "zmm3"_L1,
    "zmm30"_L1, "zmm31"_L1, "zmm4"_L1, "zmm5"_L1, "zmm6"_L1, "zmm7"_L1, "zmm8"_L1, "zmm9"_L1
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

    //  Grossschreibung ist in Assembly ueblich (MASM schreibt MOV), deshalb
    //  ohne Ruecksicht auf Gross-/Kleinschreibung. `#` gilt hier ABSICHTLICH
    //  nicht als Kommentar: auf aarch64 steht es vor jedem unmittelbaren Wert
    //  (`[sp, #-16]!`), und ein Kommentarzeichen wuerde den halben Befehl
    //  verschlucken. Bleiben `//` und `/* */`, die GAS auf beiden Befehlssaetzen
    //  nimmt.
    { .id = "asm"_L1, .label = "Assembly"_L1, .kind = ScannerKind::CLike,
      .keywords = wl(k_kw_asm), .types = wl(k_ty_asm),
      .lineComment = "//"_L1,
      .blockOpen = "/*"_L1, .blockClose = "*/"_L1, .caseSensitive = false },

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
    { "md"_L1, "markdown"_L1 },   { "markdown"_L1, "markdown"_L1 },
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
    { "s"_L1, "asm"_L1 },      { "asm"_L1, "asm"_L1 },   { "inc"_L1, "asm"_L1 },
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
