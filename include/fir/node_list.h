#ifndef FIR_NODE_LIST_H
#define FIR_NODE_LIST_H

/// \file
///
/// This file lists all the nodes of the IR, grouped by category. The lists come in the form of
/// macros that can be passed another macro argument which determines how the list is expanded. This
/// allows generating some code automatically.

/// Lists all types.
#define FIR_TYPE_LIST(x) \
    x(MEM_TY,      "mem") \
    x(ERR_TY,      "err") \
    x(PTR_TY,      "ptr") \
    x(NORET_TY,    "noret") \
    x(INT_TY,      "int") \
    x(FLOAT_TY,    "float") \
    x(TUP_TY,      "tup") \
    x(ARRAY_TY,    "array") \
    x(DYNARRAY_TY, "dynarray") \
    x(FUNC_TY,     "func")

/// List all nominal nodes. \see module.h
#define FIR_NOMINAL_NODE_LIST(x) \
    x(GLOBAL, "global") \
    x(FUNC,   "func")

/// List all arithmetic operations on integers, excluding division and remainder.
#define FIR_IARITH_OP_LIST(x) \
    x(IADD, "iadd") \
    x(ISUB, "isub") \
    x(IMUL, "imul")

/// List all arithmetic operations on floating-point numbers, excluding division and remainder.
#define FIR_FARITH_OP_LIST(x) \
    x(FADD, "fadd") \
    x(FSUB, "fsub") \
    x(FMUL, "fmul")

/// List all division and remainder operations on integers.
#define FIR_IDIV_OP_LIST(x) \
    x(SDIV, "sdiv") \
    x(UDIV, "udiv") \
    x(SREM, "srem") \
    x(UREM, "urem")

/// List all division and remainder operations on floating-point numbers.
#define FIR_FDIV_OP_LIST(x) \
    x(FDIV, "fdiv") \
    x(FREM, "frem")

/// List all integer comparison instructions.
#define FIR_ICMP_OP_LIST(x) \
    x(ICMPEQ, "icmpeq") \
    x(ICMPNE, "icmpne") \
    x(UCMPGT, "ucmpgt") \
    x(UCMPGE, "ucmpge") \
    x(UCMPLT, "ucmplt") \
    x(UCMPLE, "ucmple") \
    x(SCMPGT, "scmpgt") \
    x(SCMPGE, "scmpge") \
    x(SCMPLT, "scmplt") \
    x(SCMPLE, "scmple")

/// List all floating-point comparison instructions.
#define FIR_FCMP_OP_LIST(x) \
    x(FCMPORD, "fcmpord") \
    x(FCMPUNO, "fcmpuno") \
    x(FCMPOEQ, "fcmpoeq") \
    x(FCMPOGT, "fcmpogt") \
    x(FCMPOGE, "fcmpoge") \
    x(FCMPOLT, "fcmpolt") \
    x(FCMPOLE, "fcmpole") \
    x(FCMPONE, "fcmpone") \
    x(FCMPUEQ, "fcmpueq") \
    x(FCMPUNE, "fcmpune") \
    x(FCMPUGT, "fcmpugt") \
    x(FCMPUGE, "fcmpuge") \
    x(FCMPULT, "fcmpult") \
    x(FCMPULE, "fcmpule")

/// List all bitwise instructions.
#define FIR_BIT_OP_LIST(x) \
    x(AND, "and") \
    x(OR,  "or") \
    x(XOR, "xor")

/// List all cast instructions.
#define FIR_CAST_OP_LIST(x) \
    x(BITCAST, "bitcast") \
    x(UTOF,    "utof") \
    x(STOF,    "stof") \
    x(FTOS,    "ftos") \
    x(FTOU,    "ftou") \
    x(FEXT,    "fext") \
    x(ZEXT,    "zext") \
    x(SEXT,    "sext") \
    x(ITRUNC,  "itrunc") \
    x(FTRUNC,  "ftrunc")

/// List all aggregate operations.
#define FIR_AGGR_OP_LIST(x) \
    x(TUP,   "tup") \
    x(ARRAY, "array") \
    x(INS,   "ins") \
    x(EXT,   "ext") \
    x(OFF,   "addrof")

/// List all memory operations.
#define FIR_MEM_OP_LIST(x) \
    x(ALLOC, "alloc") \
    x(LOAD,  "load") \
    x(STORE, "store")

/// List all control-flow and function-related operations.
#define FIR_CONTROL_OP_LIST(x) \
    x(PARAM, "param") \
    x(START, "start") \
    x(CALL, "call") \
    x(LOOP, "loop") \
    x(IF,   "if")

/// List all constants.
#define FIR_CONST_LIST(x) \
    x(TOP, "top") \
    x(BOT, "bot") \
    x(CONST, "const")

/// List all IR nodes.
#define FIR_NODE_LIST(x) \
    FIR_TYPE_LIST(x) \
    FIR_CONST_LIST(x) \
    FIR_NOMINAL_NODE_LIST(x) \
    FIR_IARITH_OP_LIST(x) \
    FIR_FARITH_OP_LIST(x) \
    FIR_IDIV_OP_LIST(x) \
    FIR_FDIV_OP_LIST(x) \
    FIR_ICMP_OP_LIST(x) \
    FIR_FCMP_OP_LIST(x) \
    FIR_BIT_OP_LIST(x) \
    FIR_CAST_OP_LIST(x) \
    FIR_AGGR_OP_LIST(x) \
    FIR_MEM_OP_LIST(x) \
    FIR_CONTROL_OP_LIST(x)

#endif
