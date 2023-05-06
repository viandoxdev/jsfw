#ifndef MACRO_UTILS_H
#define MACRO_UTILS_H

#define CALL(m, ...) m(__VA_ARGS__)

#define EMPTY()

#define EVAL(...) EVAL32(__VA_ARGS__)
#define EVAL1024(...) EVAL512(EVAL512(__VA_ARGS__))
#define EVAL512(...) EVAL256(EVAL256(__VA_ARGS__))
#define EVAL256(...) EVAL128(EVAL128(__VA_ARGS__))
#define EVAL128(...) EVAL64(EVAL64(__VA_ARGS__))
#define EVAL64(...) EVAL32(EVAL32(__VA_ARGS__))
#define EVAL32(...) EVAL16(EVAL16(__VA_ARGS__))
#define EVAL16(...) EVAL8(EVAL8(__VA_ARGS__))
#define EVAL8(...) EVAL4(EVAL4(__VA_ARGS__))
#define EVAL4(...) EVAL2(EVAL2(__VA_ARGS__))
#define EVAL2(...) EVAL1(EVAL1(__VA_ARGS__))
#define EVAL1(...) __VA_ARGS__
#define EVAL0(...)

#define SND(a, b, ...) b
#define FST(a, ...) a
#define CAT(a, b) a##b

#define PROBE() ~, 1
#define IS_PROBE(...) SND(__VA_ARGS__, 0)
// _FAST_NOT(0) -> 1 _FAST_NOT(1) -> 0
#define _FAST_NOT(x) CAT(_FAST_NOT_, x)()
#define _FAST_NOT_0() 1
#define _FAST_NOT_1() 0
// NOT(0) -> 1 NOT(...) -> 0
#define NOT(x) IS_PROBE(CAT(_NOT_, x))
#define _NOT_0 PROBE()
// BOOL(0) -> 0 BOOL(...) -> 1
#define BOOL(x) _FAST_NOT(NOT(x))

// Same as EVAL1 but different meaning
#define KEEP(...) __VA_ARGS__
// Drop / Delete the arguments
#define DROP(...)

#define IF_ELSE(c) FAST_IF_ELSE(BOOL(c))
// IF_ELSE if c is know to be 0 or 1
#define FAST_IF_ELSE(c) CAT(_IF_ELSE_, c)
#define _IF_ELSE_0(...) KEEP
#define _IF_ELSE_1(...) __VA_ARGS__ DROP

#define HAS_ARGS(...) BOOL(FST(_HAS_ARGS_ __VA_ARGS__)())
#define _HAS_ARGS_() 0
#define IF_ELSE_ARGS(...) FAST_IF_ELSE(HAS_ARGS(__VA_ARGS__))

#define DEFER1(x) x EMPTY()
#define DEFER2(x) x EMPTY EMPTY()()
#define DEFER3(x) x EMPTY EMPTY EMPTY()()()
#define DEFER4(x) x EMPTY EMPTY EMPTY EMPTY()()()()
#define DEFER5(x) x EMPTY EMPTY EMPTY EMPTY EMPTY()()()()()

#define MAP(m, fst, ...) m(fst) __VA_OPT__(DEFER1(_MAP)()(DEFER1(m), __VA_ARGS__))
#define _MAP() MAP

#define REVERSE(...) IF_ELSE_ARGS(__VA_ARGS__)(EVAL(_REVERSE(__VA_ARGS__)))()
#define _REVERSE(a, ...) __VA_OPT__(DEFER1(__REVERSE)()(__VA_ARGS__), ) a
#define __REVERSE() _REVERSE

#endif
