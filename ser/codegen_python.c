#include "codegen_python.h"

#include <stddef.h>

static void write_type_name(Writer *w, TypeObject *type, Hashmap *defined) {
    if (type->kind == TypePrimitif) {
#define _case(x, str) \
    case Primitif_##x: \
        wt_format(w, str); \
        return
        switch (type->type.primitif) {
            _case(u8, "int");
            _case(u16, "int");
            _case(u32, "int");
            _case(u64, "int");
            _case(i8, "int");
            _case(i16, "int");
            _case(i32, "int");
            _case(i64, "int");
            _case(f32, "float");
            _case(f64, "float");
            _case(bool, "bool");
            _case(char, "str");
        }
#undef _case
    } else if (type->kind == TypeArray) {
        // If we have an array of char
        if (type->type.array.type->kind == TypePrimitif && type->type.array.type->type.primitif == Primitif_char) {
            wt_format(w, "str");
            return;
        }
        wt_format(w, "List[");
        write_type_name(w, type->type.array.type, defined);
        wt_format(w, "]");
    } else if (type->kind == TypeStruct) {
        StructObject *obj = (StructObject *)&type->type.struct_;
        if (defined == NULL || hashmap_has(defined, &obj)) {
            wt_write(w, obj->name.ptr, obj->name.len);
        } else {
            wt_format(w, "'%.*s'", obj->name.len, obj->name.ptr);
        }
    }
}

typedef enum {
    Read,
    Write,
} Access;

static void write_field_accessor(Writer *w, const char *base, FieldAccessor fa, TypeObject *type, Access access) {
    if (fa.indices.len == 0) {
        wt_format(w, "%s", base);
        return;
    }

    BufferedWriter b = buffered_writer_init();
    Writer *w2 = (Writer *)&b;

    bool len = false;
    wt_format(w2, "%s", base);
    for (size_t i = 0; i < fa.indices.len; i++) {
        uint64_t index = fa.indices.data[i];

        if (type->kind == TypeStruct) {
            StructObject *obj = (StructObject *)&type->type.struct_;
            Field f = obj->fields.data[index];
            wt_format(w2, ".%.*s", f.name.len, f.name.ptr);

            type = f.type;
        } else if (type->kind == TypeArray) {
            if (type->type.array.sizing == SizingMax) {
                len = index == 0;
                break;
            } else {
                wt_format(w2, "[%lu]", index);
                type = type->type.array.type;
            }
        }
    }

    if (len) {
        if (access == Read) {
            wt_format(w, "len(%.*s)", b.buf.len, b.buf.data);
        } else {
            CharVec buf = b.buf;
            for (size_t i = 0; i < buf.len; i++) {
                if (buf.data[i] == '.') {
                    buf.data[i] = '_';
                }
            }
            wt_write(w, buf.data, buf.len);
            wt_format(w, "_len");
        }
    } else {
        wt_write(w, b.buf.data, b.buf.len);
    }

    buffered_writer_drop(b);
}

static bool field_accessor_is_array_length(FieldAccessor fa, TypeObject *type) {
    if (fa.indices.len == 0 || fa.indices.data[fa.indices.len - 1] != 0 || fa.type->kind != TypePrimitif)
        return false;

    for (size_t i = 0; i < fa.indices.len - 1; i++) {
        uint64_t index = fa.indices.data[i];
        if (type->kind == TypeStruct) {
            StructObject *s = (StructObject *)&type->type.struct_;

            type = s->fields.data[index].type;
        } else if (type->kind == TypeArray) {
            type = type->type.array.type;
        }
    }

    return type->kind == TypeArray && type->type.array.sizing == SizingMax;
}

// Find the index of the FieldAccessor that points to the length of this array
static uint64_t get_array_length_field_index(Layout *layout, FieldAccessor fa) {
    size_t index = SIZE_MAX;
    for (size_t j = 0; j < layout->fields.len && layout->fields.data[j].size != 0; j++) {
        FieldAccessor f = layout->fields.data[j];
        if (f.indices.len != fa.indices.len)
            continue;

        // Check the indices for equality, all but the last one should be equal
        bool equal = true;
        for (size_t k = 0; k < f.indices.len - 1; k++) {
            if (f.indices.data[k] != fa.indices.data[k]) {
                equal = false;
                break;
            }
        }

        if (equal && f.indices.data[f.indices.len - 1] == 0) {
            index = j;
            break;
        }
    }

    if (index == SIZE_MAX) {
        log_error("No length accessor for variable size array accessor");
        exit(1);
    }

    return index;
}

static void write_type_uninit(Writer *u, TypeObject *type) {
    if (type->kind == TypeStruct) {
        StructObject *s = (StructObject *)&type->type.struct_;
        for (size_t i = 0; i < s->fields.len; i++) {
            Field f = s->fields.data[i];
            if (f.type->kind == TypeStruct) {
                StringSlice tname = f.type->type.struct_.name;
                wt_format(u, "%*sself.%.*s = %.*s.uninit()\n", INDENT * 2, "", f.name.len, f.name.ptr, tname.len, tname.ptr);
            } else if (f.type->kind == TypeArray) {
                wt_format(u, "%*sself.%.*s = []\n", INDENT * 2, "", f.name.len, f.name.ptr);
            }
        }
    }
}

static void write_type_funcs(
    Writer *s,
    Writer *d,
    TypeObject *type,
    const char *base,
    CurrentAlignment al,
    Hashmap *layouts,
    size_t indent,
    size_t depth,
    bool always_inline
) {
    Layout *layout = hashmap_get(layouts, &(Layout){.type = type});
    assert(layout != NULL, "Type has no layout");

    if (layout->fields.len == 0)
        return;

    Alignment align = al.align;
    size_t offset = calign_to(al, layout->fields.data[0].type->align);

    if (offset > 0) {
        wt_format(s, "%*sbuf += bytes(%lu)\n", indent, "", offset);
        wt_format(d, "%*soff += %lu\n", indent, "", offset);
    }

    if (type->kind == TypeStruct && !always_inline) {
        offset = calign_to(al, layout->type->align);
        if (offset != 0) {
            wt_format(s, "%*sbuf += bytes(%lu)\n", indent, "", offset);
            wt_format(d, "%*soff = %lu\n", indent, "", offset);
        }
        wt_format(s, "%*s%s.serialize(buf)\n", indent, "", base);
        wt_format(d, "%*soff += %s.deserialize(buf[off:])\n", indent, "", base);
        return;
    }

    wt_format(s, "%*sbuf += pack('<", indent, "");
    wt_format(d, "%*sxs%lu = unpack('<", indent, "", depth);
    al = calign_add(al, offset);

    size_t size = 0;
    size_t i = 0;
    for (; i < layout->fields.len && layout->fields.data[i].size != 0; i++) {
        FieldAccessor fa = layout->fields.data[i];
        assert(fa.type->kind == TypePrimitif, "Field accessor of non zero size doesn't point to primitive type");

#define _case(x, f) \
    case Primitif_##x: \
        wt_format(s, f); \
        wt_format(d, f); \
        break
        switch (fa.type->type.primitif) {
            _case(u8, "B");
            _case(u16, "H");
            _case(u32, "I");
            _case(u64, "Q");
            _case(i8, "b");
            _case(i16, "h");
            _case(i32, "i");
            _case(i64, "q");
            _case(f32, "f");
            _case(f64, "d");
            _case(bool, "?");
            _case(char, "c");
        }
#undef _case
        al = calign_add(al, fa.size);
        offset += fa.size;
        size += fa.size;
    }

    size_t padding = 0;
    if (i < layout->fields.len) {
        padding = calign_to(al, layout->fields.data[i].type->align);
        wt_format(s, "%lux", padding);
    }

    wt_format(s, "',\n");
    wt_format(d, "', buf[off:off + %lu])\n", size);

    for (size_t j = 0; j < i; j++) {
        FieldAccessor fa = layout->fields.data[j];

        wt_format(s, "%*s            ", indent, "");
        write_field_accessor(s, base, fa, type, Read);
        if (fa.type->kind == TypePrimitif && fa.type->type.primitif == Primitif_char) {
            wt_format(s, ".encode(encoding='ASCII', errors='replace')");
        }
        if (j < i - 1) {
            wt_format(s, ",\n");
        } else {
            wt_format(s, ")\n");
        }

        if (!field_accessor_is_array_length(fa, type)) {
            wt_format(d, "%*s", indent, "");
            write_field_accessor(d, base, fa, type, Write);
            wt_format(d, " = xs%lu[%lu]", depth, j);
            if (fa.type->kind == TypePrimitif && fa.type->type.primitif == Primitif_char) {
                wt_format(d, ".decode(encoding='ASCII', errors='replace')");
            }
            wt_format(d, "\n");
        }
    }

    if (i < layout->fields.len) {
        wt_format(d, "%*soff += %lu\n", indent, "", padding + size);
    } else {
        wt_format(d, "%*soff += %lu\n", indent, "", padding + size + calign_to(al, align));
    }

    bool alignment_unknown = false;
    for (; i < layout->fields.len; i++) {
        alignment_unknown = true;

        FieldAccessor fa = layout->fields.data[i];
        uint64_t len_index = get_array_length_field_index(layout, fa);

        if (fa.type->kind == TypePrimitif && fa.type->type.primitif == Primitif_char) {
            wt_format(s, "%*sbuf += ", indent, "");
            wt_format(d, "%*s", indent, "");
            write_field_accessor(s, base, fa, type, Read);
            write_field_accessor(d, base, fa, type, Write);

            wt_format(d, " = buf[off:off + xs%lu[%lu]].decode(encoding='ASCII', errors='replace')\n", depth, len_index);
            wt_format(d, "%*soff += xs%lu[%lu]\n", indent, "", depth, len_index);
            wt_format(s, ".encode(encoding='ASCII', errors='replace')\n");
            continue;
        }

        wt_format(s, "%*sfor e%lu in ", indent, "", depth);
        write_field_accessor(s, base, fa, type, Read);
        wt_format(s, ":\n");
        wt_format(d, "%*s", indent, "");
        write_field_accessor(d, base, fa, type, Write);
        wt_format(d, " = []\n");
        wt_format(d, "%*sfor _ in range(xs%lu[%lu]):\n", indent, "", depth, len_index);
        if (fa.type->kind == TypeArray && fa.type->type.array.sizing == SizingFixed) {
            wt_format(d, "%*se%lu = [None] * %lu\n", indent + INDENT, "", depth, fa.type->type.array.size);
        } else if (fa.type->kind == TypeStruct) {
            struct StructObject s = fa.type->type.struct_;
            wt_format(d, "%*se%lu = %.*s.uninit()\n", indent + INDENT, "", depth, s.name.len, s.name.ptr);
        }
        char *new_base = msprintf("e%lu", depth);
        write_type_funcs(
            s,
            d,
            fa.type,
            new_base,
            (CurrentAlignment){.align = fa.type->align, .offset = 0},
            layouts,
            indent + INDENT,
            depth + 1,
            false
        );
        wt_format(d, "%*s", indent + INDENT, "");
        write_field_accessor(d, base, fa, type, Write);
        wt_format(d, ".append(%s)\n", new_base);
        free(new_base);
    }

    if (alignment_unknown) {
        wt_format(s, "%*sbuf += bytes((%u - len(buf)) & %u)\n", indent, "", align.value, align.mask);
        wt_format(d, "%*soff += (%u - off) & %u\n", indent, "", align.value, align.mask);
    }
}

static void write_struct_class(Writer *w, StructObject *obj, Hashmap *defined, Hashmap *layouts) {
    TypeObject *type = (void *)((byte *)obj - offsetof(TypeObject, type));

    wt_format(w, "@dataclass\n");
    wt_format(w, "class %.*s:\n", obj->name.len, obj->name.ptr);
    for (size_t i = 0; i < obj->fields.len; i++) {
        Field f = obj->fields.data[i];
        wt_format(w, "%*s%.*s: ", INDENT, "", f.name.len, f.name.ptr);
        write_type_name(w, f.type, defined);
        wt_format(w, "\n");
    }
    BufferedWriter ser = buffered_writer_init();
    BufferedWriter deser = buffered_writer_init();
    write_type_funcs(
        (Writer *)&ser,
        (Writer *)&deser,
        type,
        "self",
        (CurrentAlignment){.align = type->align, .offset = 0},
        layouts,
        INDENT * 2,
        0,
        true
    );

    wt_format(w, "%*s\n", INDENT, "");
    wt_format(w, "%*s@classmethod\n", INDENT, "");
    wt_format(w, "%*sdef uninit(cls) -> '%.*s':\n", INDENT, "", obj->name.len, obj->name.ptr);
    wt_format(w, "%*sself = cls.__new__(cls)\n", INDENT * 2, "");
    write_type_uninit(w, type);
    wt_format(w, "%*sreturn self\n", INDENT * 2, "");
    wt_format(w, "%*s\n", INDENT, "");
    wt_format(w, "%*sdef serialize(self, buf: bytearray):\n", INDENT, "");
    wt_format(w, "%*sbase = len(buf)\n", INDENT * 2, "");
    wt_write(w, ser.buf.data, ser.buf.len);
    wt_format(w, "%*sreturn len(buf) - base\n", INDENT * 2, "");
    wt_format(w, "%*s\n", INDENT, "");
    wt_format(w, "%*sdef deserialize(self, buf: bytes):\n", INDENT, "");
    wt_format(w, "%*soff = 0\n", INDENT * 2, "");
    wt_write(w, deser.buf.data, deser.buf.len);
    wt_format(w, "%*sreturn off\n", INDENT * 2, "");
    wt_format(w, "\n");

    buffered_writer_drop(ser);
    buffered_writer_drop(deser);

    hashmap_set(defined, &obj);
}

typedef struct {
    Hashmap *layouts;
    Hashmap *defined;
} CallbackData;

static void write_struct(Writer *w, StructObject *obj, void *user_data) {
    CallbackData *data = user_data;
    write_struct_class(w, obj, data->defined, data->layouts);
}

static void define_struct_classes(Writer *w, Program *p) {
    Hashmap *defined = hashmap_init(pointer_hash, pointer_equal, NULL, sizeof(StructObject *));
    CallbackData data = {.defined = defined, .layouts = p->layouts};
    define_structs(p, w, write_struct, &data);
    hashmap_drop(defined);
}

static void define_message(Writer *w, const char *prefix, uint16_t tag, Hashmap *layouts, MessageObject msg, uint64_t version) {
    char *name = msprintf("%s%.*s", prefix, msg.name.len, msg.name.ptr);
    StringSlice name_slice = {.ptr = name, .len = strlen(name)};

    wt_format(w, "@dataclass\n");
    wt_format(w, "class %s(%sMessage):\n", name, prefix);

    TypeObject *type;
    FieldVec fields = vec_clone(&msg.fields);
    {
        if (msg.attributes & Attr_versioned) {
            Field f = {.name = STRING_SLICE("_version"), .type = (TypeObject *)&PRIMITIF_u64};
            vec_push(&fields, f);
        }

        type = malloc(sizeof(TypeObject));
        assert_alloc(type);
        type->kind = TypeStruct;
        type->type.struct_.name = name_slice;
        type->type.struct_.has_funcs = false;
        type->type.struct_.fields = *(AnyVec *)&fields;
        type->align = ALIGN_8;

        Layout l = type_layout(type);
        hashmap_set(layouts, &l);
    }

    for (size_t i = 0; i < msg.fields.len; i++) {
        Field f = msg.fields.data[i];
        wt_format(w, "%*s%.*s: ", INDENT, "", f.name.len, f.name.ptr);
        write_type_name(w, f.type, NULL);
        wt_format(w, "\n");
    }

    if (msg.attributes & Attr_versioned) {
        wt_format(w, "%*s_version: int = %lu\n", INDENT, "", version);
    }

    BufferedWriter ser = buffered_writer_init();
    BufferedWriter deser = buffered_writer_init();
    write_type_funcs(
        (Writer *)&ser,
        (Writer *)&deser,
        type,
        "self",
        (CurrentAlignment){.align = type->align, .offset = 2},
        layouts,
        INDENT * 2,
        0,
        true
    );

    wt_format(w, "%*s\n", INDENT, "");
    wt_format(w, "%*s@classmethod\n", INDENT, "");
    wt_format(w, "%*sdef uninit(cls) -> '%s':\n", INDENT, "", name);
    wt_format(w, "%*sself = cls.__new__(cls)\n", INDENT * 2, "");
    write_type_uninit(w, type);
    wt_format(w, "%*sreturn self\n", INDENT * 2, "");
    wt_format(w, "%*s\n", INDENT, "");
    wt_format(w, "%*sdef serialize(self, buf: bytearray):\n", INDENT, "");
    wt_format(w, "%*sbase = len(buf)\n", INDENT * 2, "");
    wt_format(w, "%*sbuf += pack('>QH', MSG_MAGIC_START, %u)\n", INDENT * 2, "", tag);
    wt_write(w, ser.buf.data, ser.buf.len);
    wt_format(w, "%*sbuf += pack('>Q', MSG_MAGIC_END)\n", INDENT * 2, "");
    wt_format(w, "%*sreturn len(buf) - base\n", INDENT * 2, "");
    wt_format(w, "%*s\n", INDENT, "");
    wt_format(w, "%*s@classmethod\n", INDENT, "");
    wt_format(w, "%*sdef _deserialize(cls, buf: bytes) -> Tuple['%s', int]:\n", INDENT, "", name);
    wt_format(w, "%*smagic_start, tag = unpack('>QH', buf[0:10])\n", INDENT * 2, "");
    wt_format(w, "%*sif magic_start != MSG_MAGIC_START or tag != %u:\n", INDENT * 2, "", tag);
    wt_format(w, "%*sraise ValueError\n", INDENT * 3, "");
    wt_format(w, "%*soff = 10\n", INDENT * 2, "");
    wt_format(w, "%*sself = %s.uninit()\n", INDENT * 2, "", name);
    wt_write(w, deser.buf.data, deser.buf.len);
    wt_format(w, "%*smagic_end = unpack('>Q', buf[off:off + 8])[0]\n", INDENT * 2, "");
    wt_format(w, "%*sif magic_end != MSG_MAGIC_END:\n", INDENT * 2, "");
    wt_format(w, "%*sraise ValueError\n", INDENT * 3, "");
    wt_format(w, "%*soff += 8\n", INDENT * 2, "");
    wt_format(w, "%*sreturn self, off\n", INDENT * 2, "");
    wt_format(w, "\n");

    buffered_writer_drop(ser);
    buffered_writer_drop(deser);
    free(name);
    vec_drop(fields);
    free(type);
}

static void define_messages(Writer *w, MessagesObject msgs, Program *p) {
    char *prefix = strndup(msgs.name.ptr, msgs.name.len);
    wt_format(w, "class %sMessage(ABC):\n", prefix);
    wt_format(w, "%*s@abstractmethod\n", INDENT, "");
    wt_format(w, "%*sdef serialize(self, buf: bytearray) -> int:\n", INDENT, "");
    wt_format(w, "%*spass\n", INDENT * 2, "");
    wt_format(w, "%*s@classmethod\n", INDENT, "");
    wt_format(w, "%*sdef deserialize(cls, buf: bytes) -> Tuple['Message', int]:\n", INDENT, "");
    wt_format(w, "%*smagic_start, tag = unpack('>QH', buf[0:10])\n", INDENT * 2, "");
    wt_format(w, "%*sif magic_start != MSG_MAGIC_START:\n", INDENT * 2, "");
    wt_format(w, "%*sraise ValueError\n", INDENT * 3, "");
    for (size_t i = 0; i < msgs.messages.len; i++) {
        if (i == 0) {
            wt_format(w, "%*sif tag == 0:\n", INDENT * 2, "");
        } else {
            wt_format(w, "%*selif tag == %lu:\n", INDENT * 2, "", i);
        }
        StringSlice name = msgs.messages.data[i].name;
        wt_format(w, "%*sreturn %s%.*s._deserialize(buf)\n", INDENT * 3, "", prefix, name.len, name.ptr);
    }
    wt_format(w, "%*selse:\n", INDENT * 2, "");
    wt_format(w, "%*sraise ValueError\n", INDENT * 3, "");

    for (size_t i = 0; i < msgs.messages.len; i++) {
        define_message(w, prefix, i, p->layouts, msgs.messages.data[i], msgs.version);
    }
    free(prefix);
}

void codegen_python(Writer *source, Program *p) {
    wt_format(
        source,
        "# generated file\n"
        "from dataclasses import dataclass\n"
        "from typing import List, Tuple\n"
        "from abc import ABC, abstractmethod\n"
        "from struct import pack, unpack\n"
        "\n"
        "MSG_MAGIC_START = 0x%016lX\n"
        "MSG_MAGIC_END = 0x%016lX\n"
        "\n",
        MSG_MAGIC_START,
        MSG_MAGIC_END
    );

    define_struct_classes(source, p);
    for (size_t i = 0; i < p->messages.len; i++) {
        define_messages(source, p->messages.data[i], p);
    }
}
