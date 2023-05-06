#include "codegen.h"

#include <ctype.h>
#include <stdarg.h>

static void buffered_writer_write(void *w, const char *data, size_t len) {
    // We don't use vec_push array because we want the string to be null terminated at all time (while not really including the
    // null character in the string / len)
    BufferedWriter *bw = (BufferedWriter *)w;
    vec_grow(&bw->buf, bw->buf.len + len + 1);
    memcpy(&bw->buf.data[bw->buf.len], data, len);
    bw->buf.data[bw->buf.len + len] = '\0';
    bw->buf.len += len;
}
static void buffered_writer_format(void *w, const char *fmt, va_list args) {
    BufferedWriter *bw = (BufferedWriter *)w;
    va_list args2;
    va_copy(args2, args);
    size_t cap = bw->buf.cap - bw->buf.len;
    char *ptr = &bw->buf.data[bw->buf.len];
    int len = vsnprintf(ptr, cap, fmt, args);
    if (cap <= len) {
        // The writing failed
        vec_grow(&bw->buf, bw->buf.len + len + 1);
        ptr = &bw->buf.data[bw->buf.len];
        vsnprintf(ptr, len + 1, fmt, args2);
    }
    va_end(args2);
    bw->buf.len += len;
}
static void file_writer_write(void *w, const char *data, size_t len) {
    FileWriter *fw = (FileWriter *)w;
    fwrite(data, 1, len, fw->fd);
}
static void file_writer_format(void *w, const char *fmt, va_list args) {
    FileWriter *fw = (FileWriter *)w;
    vfprintf(fw->fd, fmt, args);
}

BufferedWriter buffered_writer_init() {
    CharVec buf = vec_init();
    vec_grow(&buf, 512);
    return (BufferedWriter){.w.write = buffered_writer_write, .w.format = buffered_writer_format, .buf = buf};
}
void buffered_writer_drop(BufferedWriter w) { vec_drop(w.buf); }
FileWriter file_writer_init(const char *path) {
    FILE *fd = fopen(path, "w");
    assert(fd != NULL, "couldn't open output file");
    return (FileWriter){.w.write = file_writer_write, .w.format = file_writer_format, .fd = fd};
}
FileWriter file_writer_from_fd(FILE *fd) {
    return (FileWriter){.w.write = file_writer_write, .w.format = file_writer_format, .fd = fd};
}
void file_writer_drop(FileWriter w) { fclose(w.fd); }

void wt_write(Writer *w, const char *data, size_t len) { w->write(w, data, len); }
void wt_format(Writer *w, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    w->format(w, fmt, args);
    va_end(args);
}

typedef struct {
    StructObject *obj;
    PointerVec dependencies;
} StructDependencies;

void strdeps_drop(void *item) {
    StructDependencies *deps = (StructDependencies *)item;
    vec_drop(deps->dependencies);
}

impl_hashmap(
    strdeps, StructDependencies, { return hash(state, (byte *)&v->obj, sizeof(StructObject *)); }, { return a->obj == b->obj; }
);

int struct_object_compare(const void *a, const void *b) {
    const StructObject *sa = *(StructObject **)a;
    const StructObject *sb = *(StructObject **)b;
    size_t len = sa->name.len < sb->name.len ? sa->name.len : sb->name.len;
    return strncmp(sa->name.ptr, sb->name.ptr, len);
}

void define_structs(Program *p, Writer *w, void (*define)(Writer *w, StructObject *obj)) {
    Hashmap *dependencies = hashmap_init(strdeps_hash, strdeps_equal, strdeps_drop, sizeof(StructDependencies));

    TypeDef *td = NULL;
    while (hashmap_iter(p->typedefs, &td)) {
        if (td->value->kind != TypeStruct)
            continue;
        StructObject *obj = (StructObject *)&td->value->type.struct_;

        StructDependencies deps = {.obj = obj, .dependencies = vec_init()};
        for (size_t i = 0; i < obj->fields.len; i++) {
            TypeObject *type = obj->fields.data[i].type;
            // Skip through the field arrays
            while (type->kind == TypeArray && !type->type.array.heap) {
                type = type->type.array.type;
            }

            if (type->kind != TypeStruct)
                continue;

            vec_push(&deps.dependencies, &type->type.struct_);
        }

        hashmap_set(dependencies, &deps);
    }

    PointerVec to_define = vec_init();
    size_t pass = 0;
    do {
        vec_clear(&to_define);

        StructDependencies *deps = NULL;
        while (hashmap_iter(dependencies, &deps)) {
            bool dependencies_met = true;
            for (size_t i = 0; i < deps->dependencies.len; i++) {
                if (hashmap_has(dependencies, &(StructDependencies){.obj = deps->dependencies.data[i]})) {
                    dependencies_met = false;
                    break;
                }
            }

            if (!dependencies_met)
                continue;
            vec_push(&to_define, deps->obj);
        }

        qsort(to_define.data, to_define.len, sizeof(StructObject *), struct_object_compare);

        for (size_t i = 0; i < to_define.len; i++) {
            StructObject *s = to_define.data[i];
            define(w, s);
            hashmap_delete(dependencies, &(StructDependencies){.obj = s});
        }
        pass++;
    } while (to_define.len > 0);

    if (dependencies->count > 0) {
        log_error("cyclic struct dependency without indirection couldn't be resolved");
    }

    hashmap_drop(dependencies);
    vec_drop(to_define);
}

char *pascal_to_snake_case(StringSlice str) {
    CharVec res = vec_init();
    vec_grow(&res, str.len + 4);
    for (size_t i = 0; i < str.len; i++) {
        if (i == 0) {
            vec_push(&res, tolower(str.ptr[i]));
            continue;
        }

        char c = str.ptr[i];
        if (isupper(c)) {
            vec_push(&res, '_');
        }
        vec_push(&res, tolower(c));
    }

    vec_push(&res, '\0');

    return res.data;
}
