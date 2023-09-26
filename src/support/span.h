#pragma once

#define SPAN_FOREACH(elem_ty, elem, span) \
    for (elem_ty* elem = (span).elems; elem != (span).elems + (span).elem_count; ++elem)

#define SPAN_DECL(name, elem_ty) \
    struct name { \
        elem_ty* elems; \
        size_t elem_count; \
    };

#define CONST_SPAN_FOREACH(elem_ty, elem, span) \
    for (elem_ty const* elem = (span).elems; elem != (span).elems + (span).elem_count; ++elem)

#define CONST_SPAN_DECL(name, elem_ty) \
    struct name { \
        elem_ty const* elems; \
        size_t elem_count; \
    };
