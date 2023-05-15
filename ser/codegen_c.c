#include "codegen_c.h"

#include "vector.h"
#include "vector_impl.h"

#include <stddef.h>

typedef enum {
    MTPointer,
    MTArray,
} ModifierType;

typedef struct {
    ModifierType type;
    uint64_t size;
} Modifier;

#define MOD_PTR ((Modifier){.type = MTPointer})
#define MOD_ARRAY(s) ((Modifier){.type = MTArray, .size = s})

VECTOR_IMPL(Modifier, ModifierVec, modifier);

static inline const char *array_size_type(uint64_t size) {
    if (size <= UINT8_MAX) {
        return "uint8_t";
    } else if (size <= UINT16_MAX) {
        return "uint16_t";
    } else if (size <= UINT32_MAX) {
        return "uint32_t";
    } else {
        return "uint64_t";
    }
}

static void write_field(Writer *w, Field f, Modifier *mods, size_t len, uint32_t indent);
// Wrte the *base* type type with indentation
static void write_type(Writer *w, TypeObject *type, uint32_t indent) {
    if (type->kind == TypePrimitif) {
#define _case(x, s) \
    case Primitif_##x: \
        wt_format(w, "%*s" #s " ", indent, ""); \
        break
        switch (type->type.primitif) {
            _case(u8, uint8_t);
            _case(u16, uint16_t);
            _case(u32, uint32_t);
            _case(u64, uint64_t);
            _case(i8, uint8_t);
            _case(i16, uint16_t);
            _case(i32, uint32_t);
            _case(i64, uint64_t);
            _case(f32, float);
            _case(f64, double);
            _case(char, char);
            _case(bool, bool);
        }
#undef _case
    } else if (type->kind == TypeStruct) {
        wt_format(w, "%*sstruct %.*s ", indent, "", type->type.struct_.name.len, type->type.struct_.name.ptr);
    } else {
        if (type->type.array.sizing == SizingMax) {
            const char *len_type = array_size_type(type->type.array.size);
            wt_format(w, "%*sstruct {\n%*s%s len;\n", indent, "", indent + INDENT, "", len_type);
            Field f = {.name = STRING_SLICE("data"), .type = type->type.array.type};
            Modifier mod;
            if (type->type.array.heap) {
                mod = MOD_PTR;
            } else {
                mod = MOD_ARRAY(type->type.array.size);
            }
            write_field(w, f, &mod, 1, indent + INDENT);
            wt_format(w, ";\n%*s} ", indent, "");
        } else {
            log_error("Called write_type on a non base type");
        }
    }
}

// Algorithm to handle c types here:
// 0. Given a field with a name and a type.
//    let a base type be a type without modifiers (any type that isn't an array with fixed sizing).
//    let modifiers be a sequence of array or pointer (array have size), the type of a modifier is
//    either array or pointer.
//    let * be a pointer modifier and [n] be an array modifier of size n
// 1. initialize a list of modifers, a base type variable,
//    current = type (of the field)
//    while(is_array(current) && is_fixed_size(current)) do
//        if is_heap(current) then
//            push(modifiers, *)
//        else
//            push(modifiers, [size(current)])
//        end
//        current = element_type(current)
//    end
//    base_type = current
// 3. we now have a base type, a list of modifiers and a field name.
// 4. emit base_type " "
// 5. walk modifiers in reverse (let m and p be the current and previous modifiers)
//    if type(m) == pointer then
//        if type(m) != type(p) then
//            emit "("
//        end
//        emit "*"
//    end
// 6. emit field_name
// 7. walk modifiers forward (let m and p be the current and previous modifiers)
//    if type(m) == array then
//        if type(m) != type(p) then
//            emit "("
//        end
//        emit "[" size(m) "]"
//    end
// 8. Examples
//   given the field:
//       foo: char&[1][2][3]&[4][5]&[6]&[7],
//   3.
//       base_type = char
//       modifiers = {*, *, [5], *, [3], [2], *}
//       field_name = foo
//       result = ""
//   4.
//       result = "char "
//   5.
//       result = "char *(*(**"
//   6.
//       result = "char *(*(**foo"
//   7.
//       result = "char *(*(**foo)[5])[3][2]"
//
//   in a trivial case the algoritm works as expected:
//       bar: char,
//   3.
//       base_type = char
//       modifiers = {}
//       field_name = bar
//       result = ""
//   4.
//       result = "char "
//   5.
//       result = "char "
//   6.
//       result = "char bar"
//   7.
//       result = "char bar"

static void write_field(Writer *w, Field f, Modifier *mods, size_t len, uint32_t indent) {
    TypeObject *type = f.type;
    ModifierVec modifiers = vec_init();
    TypeObject *current = type;
    _vec_modifier_push_array(&modifiers, mods, len);
    while (current->kind == TypeArray && current->type.array.sizing == SizingFixed) {
        if (current->type.array.heap) {
            _vec_modifier_push(&modifiers, MOD_PTR);
        } else {
            _vec_modifier_push(&modifiers, MOD_ARRAY(current->type.array.size));
        }
        current = current->type.array.type;
    }
    TypeObject *base_type = current;

    write_type(w, base_type, indent);
    for (int i = modifiers.len - 1; i >= 0; i--) {
        Modifier m = modifiers.data[i];
        if (m.type != MTPointer)
            continue;
        if (i == modifiers.len - 1 || modifiers.data[i + 1].type == m.type) {
            wt_format(w, "*");
        } else {
            wt_format(w, "(*");
        }
    }
    wt_format(w, "%.*s", f.name.len, f.name.ptr);
    for (size_t i = 0; i < modifiers.len; i++) {
        Modifier m = modifiers.data[i];
        if (m.type != MTArray)
            continue;
        if (i == 0 || modifiers.data[i - 1].type == m.type) {
            wt_format(w, "[%lu]", m.size);
        } else {
            wt_format(w, ")[%lu]", m.size);
        }
    }
    _vec_modifier_drop(modifiers);
}

static void write_struct(Writer *w, StructObject *obj, void *_user_data) {
    wt_format(w, "typedef struct %.*s {\n", obj->name.len, obj->name.ptr);

    for (size_t i = 0; i < obj->fields.len; i++) {
        Field f = obj->fields.data[i];
        write_field(w, f, NULL, 0, INDENT);
        wt_format(w, ";\n", f.name.len, f.name.ptr);
    }

    wt_format(w, "} %.*s;\n\n", obj->name.len, obj->name.ptr);
}

static void write_align(Writer *w, const char *var, const Alignment align, size_t indent) {
    wt_format(w, "%*s%s = (byte*)(((((uintptr_t)%s - 1) >> %u) + 1) << %u);\n", indent, "", var, var, align.po2, align.po2);
}

static void write_accessor(Writer *w, TypeObject *base_type, FieldAccessor fa, bool ptr) {
    if (fa.indices.len == 0)
        return;

    if (ptr) {
        wt_write(w, "->", 2);
    } else {
        wt_write(w, ".", 1);
    }

    TypeObject *t = base_type;
    for (size_t j = 0; j < fa.indices.len; j++) {
        uint64_t index = fa.indices.data[j];

        if (t->kind == TypeStruct) {
            if (j != 0)
                wt_write(w, ".", 1);
            StructObject *st = (StructObject *)&t->type.struct_;
            wt_write(w, st->fields.data[index].name.ptr, st->fields.data[index].name.len);
            t = st->fields.data[index].type;
        } else if (t->kind == TypeArray) {
            if (t->type.array.sizing == SizingMax) {
                if (j != 0)
                    wt_write(w, ".", 1);
                if (index == 0) {
                    uint64_t size = t->type.array.size;
                    wt_write(w, "len", 3);
                    const TypeObject *type;
                    if (size <= UINT8_MAX) {
                        type = &PRIMITIF_u8;
                    } else if (size <= UINT16_MAX) {
                        type = &PRIMITIF_u16;
                    } else if (size <= UINT32_MAX) {
                        type = &PRIMITIF_u32;
                    } else {
                        type = &PRIMITIF_u64;
                    }
                    t = (TypeObject *)type;
                } else {
                    wt_write(w, "data", 4);
                    t = t->type.array.type;
                }
            } else {
                wt_format(w, "[%lu]", index);
                t = t->type.array.type;
            }
        }
    }
}

static bool is_field_accessor_heap_array(FieldAccessor fa, TypeObject *base_type) {
    if (fa.indices.len == 0)
        return base_type->kind == TypeArray && base_type->type.array.heap;

    // In the case of a heap array the last index will choose between length and data,
    // but since we only care about the array
    fa.indices.len--;

    TypeObject *t = base_type;
    for (size_t i = 0; i < fa.indices.len; i++) {
        uint64_t index = fa.indices.data[i];

        if (t->kind == TypeStruct) {
            StructObject *st = (StructObject *)&t->type.struct_;
            t = st->fields.data[index].type;
        } else if (t->kind == TypeArray) {
            if (t->type.array.sizing == SizingMax) {
                if (index == 0) {
                    return false;
                } else {
                    t = t->type.array.type;
                }
            } else {
                t = t->type.array.type;
            }
        }
    }

    return t->kind == TypeArray && t->type.array.heap;
}

static void write_type_serialization(
    Writer *w, const char *base, bool ptr, Layout *layout, CurrentAlignment al, Hashmap *layouts, size_t indent, size_t depth, bool always_inline
) {
    if (layout->fields.len == 0)
        return;

    Alignment align = al.align;
    size_t offset = al.offset;

    offset += calign_to(al, layout->fields.data[0].type->align);

    if (layout->type->kind == TypeStruct && layout->type->type.struct_.has_funcs && !always_inline) {
        char *name = pascal_to_snake_case(layout->type->type.struct_.name);
        char *deref = ptr ? "*" : "";
        wt_format(w, "%*sbuf += %s_serialize(%s%s, &buf[%lu]);\n", indent, "", name, deref, base, offset);
        free(name);
        return;
    }

    size_t i = 0;
    for (; i < layout->fields.len && layout->fields.data[i].size != 0; i++) {
        FieldAccessor fa = layout->fields.data[i];
        wt_format(w, "%*s*(", indent, "");
        write_type(w, fa.type, 0);
        wt_format(w, "*)&buf[%lu] = %s", offset, base);
        write_accessor(w, layout->type, fa, ptr);
        wt_write(w, ";\n", 2);

        offset += fa.size;
        al = calign_add(al, fa.size);
    }

    if (i < layout->fields.len) {
        offset += calign_to(al, layout->fields.data[i].type->align);
        wt_format(w, "%*sbuf += %lu;\n", indent, "", offset);

        for (; i < layout->fields.len; i++) {
            FieldAccessor farr = layout->fields.data[i];
            FieldAccessor flen = field_accessor_clone(&farr);
            // Access the length instead of data
            flen.indices.data[flen.indices.len - 1] = 0;

            wt_format(w, "%*sfor(size_t i = 0; i < %s", indent, "", base);
            write_accessor(w, layout->type, flen, ptr);
            field_accessor_drop(flen);
            char *vname = msprintf("e%lu", depth);
            wt_format(w, "; i++) {\n%*stypeof(%s", indent + INDENT, "", base);
            write_accessor(w, layout->type, farr, ptr);
            wt_format(w, "[i]) %s = %s", vname, base);
            write_accessor(w, layout->type, farr, ptr);
            wt_format(w, "[i];\n");

            Layout *arr_layout = hashmap_get(layouts, &(Layout){.type = farr.type});
            assert(arr_layout != NULL, "Type has no layout (How ?)");
            write_type_serialization(
                w,
                vname,
                false,
                arr_layout,
                (CurrentAlignment){.align = farr.type->align, .offset = 0},
                layouts,
                indent + INDENT,
                depth + 1,
                false
            );
            wt_format(w, "%*s}\n", indent, "");
            free(vname);
        }
        write_align(w, "buf", align, indent);
    } else {
        offset += calign_to(al, align);
        wt_format(w, "%*sbuf += %lu;\n", indent, "", offset);
    }
}

static void write_type_deserialization(
    Writer *w, const char *base, bool ptr, Layout *layout, CurrentAlignment al, Hashmap *layouts, size_t indent, size_t depth, bool always_inline
) {
    if (layout->fields.len == 0)
        return;

    Alignment align = al.align;
    size_t offset = al.offset;

    offset += calign_to(al, layout->fields.data[0].type->align);

    if (layout->type->kind == TypeStruct && layout->type->type.struct_.has_funcs && !always_inline) {
        char *name = pascal_to_snake_case(layout->type->type.struct_.name);
        char *ref = ptr ? "" : "&";
        wt_format(w, "%*sbuf += %s_deserialize(%s%s, &buf[%lu]);\n", indent, "", name, ref, base, offset);
        free(name);
        return;
    }

    char *deref = "";
    if (layout->type->kind == TypePrimitif) {
        deref = "*";
    }

    size_t i = 0;
    for (; i < layout->fields.len && layout->fields.data[i].size != 0; i++) {
        FieldAccessor fa = layout->fields.data[i];
        wt_format(w, "%*s%s%s", indent, "", deref, base);
        write_accessor(w, layout->type, fa, ptr);
        wt_format(w, " = *(");
        write_type(w, fa.type, 0);
        wt_format(w, "*)&buf[%lu]", offset, base);
        wt_write(w, ";\n", 2);

        offset += fa.size;
        al = calign_add(al, fa.size);
    }

    if (i < layout->fields.len) {
        offset += calign_to(al, layout->fields.data[i].type->align);
        wt_format(w, "%*sbuf += %lu;\n", indent, "", offset);

        for (; i < layout->fields.len; i++) {
            FieldAccessor farr = layout->fields.data[i];
            FieldAccessor flen = field_accessor_clone(&farr);
            // Access the length instead of data
            flen.indices.data[flen.indices.len - 1] = 0;

            if (is_field_accessor_heap_array(farr, layout->type)) {
                wt_format(w, "%*s%s", indent, "", base);
                write_accessor(w, layout->type, farr, ptr);
                wt_format(w, " = malloc(%s", base);
                write_accessor(w, layout->type, flen, ptr);
                wt_format(w, " * sizeof(typeof(*%s", base);
                write_accessor(w, layout->type, farr, ptr);
                wt_format(w, ")));\n");
            }
            wt_format(w, "%*sfor(size_t i = 0; i < %s", indent, "", base);
            write_accessor(w, layout->type, flen, ptr);
            field_accessor_drop(flen);
            char *vname = msprintf("e%lu", depth);
            wt_format(w, "; i++) {\n%*stypeof(&%s", indent + INDENT, "", base);
            write_accessor(w, layout->type, farr, ptr);
            wt_format(w, "[i]) %s = &%s", vname, base);
            write_accessor(w, layout->type, farr, ptr);
            wt_format(w, "[i];\n");

            Layout *arr_layout = hashmap_get(layouts, &(Layout){.type = farr.type});
            assert(arr_layout != NULL, "Type has no layout (How ?)");
            write_type_deserialization(
                w,
                vname,
                true,
                arr_layout,
                (CurrentAlignment){.align = farr.type->align, .offset = 0},
                layouts,
                indent + INDENT,
                depth + 1,
                false
            );
            wt_format(w, "%*s}\n", indent, "");
            free(vname);
        }
        write_align(w, "buf", align, indent);
    } else {
        offset += calign_to(al, align);
        wt_format(w, "%*sbuf += %lu;\n", indent, "", offset);
    }
}

static int write_type_free(Writer *w, const char *base, TypeObject *type, Hashmap *layouts, size_t indent, size_t depth, bool always_inline) {
    if (type->kind == TypePrimitif) {
        return 0;
    } else if (type->kind == TypeArray) {
        BufferedWriter b = buffered_writer_init();
        Writer *w2 = (Writer *)&b;

        int total = 0;

        wt_format(w2, "%*sfor(size_t i = 0; i < ", indent, "");
        if (type->type.array.sizing == SizingMax) {
            wt_format(w2, "%s.len; i++) {\n", base);
            wt_format(w2, "%*stypeof(%s.data[i]) e%lu = %s.data[i];\n", indent + INDENT, "", base, depth, base);
        } else {
            wt_format(w2, "%lu; i++) {\n", type->type.array.size);
            wt_format(w2, "%*stypeof(%s[i]) e%lu = %s[i];\n", indent + INDENT, "", base, depth, base);
        }

        char *new_base = msprintf("e%lu", depth);
        total += write_type_free(w2, new_base, type->type.array.type, layouts, indent + INDENT, depth + 1, false);
        free(new_base);
        wt_format(w2, "%*s}\n", indent, "");

        if (total > 0) {
            wt_write(w, b.buf.data, b.buf.len);
        }
        buffered_writer_drop(b);

        if (type->type.array.heap) {
            wt_format(w, "%*sfree(%s.data);\n", indent, "", base);
            total++;
        }

        return total;
    } else if (type->kind == TypeStruct) {
        StructObject *s = (StructObject *)&type->type.struct_;
        int total = 0;

        if(type->type.struct_.has_funcs && !always_inline) {
            char *name = pascal_to_snake_case(s->name);
            wt_format(w, "%*s%s_free(%s);\n", indent, "", name, base);
            free(name);

            Layout *layout = hashmap_get(layouts, &(Layout){.type = type});
            assert(layout != NULL, "No layout for type that has funcs defined");
            for(size_t i = 0; i < layout->fields.len; i++) {
                if(layout->fields.data[i].size == 0) total++;
            }

            return total;
        }

        for (size_t i = 0; i < s->fields.len; i++) {
            Field f = s->fields.data[i];
            char *new_base = msprintf("%s.%.*s", base, f.name.len, f.name.ptr);
            total += write_type_free(w, new_base, f.type, layouts, indent, depth, false);
            free(new_base);
        }

        return total;
    }

    return 0;
}

static void write_struct_func_decl(Writer *w, StructObject *obj, void *_user_data) {
    obj->has_funcs = true;

    StringSlice sname = obj->name;
    char *snake_case_name = pascal_to_snake_case(sname);
    wt_format(w, "__attribute__((unused)) static int %s_serialize(struct %.*s val, byte *buf);\n", snake_case_name, sname.len, sname.ptr);
    wt_format(w, "__attribute__((unused)) static int %s_deserialize(struct %.*s *val, const byte *buf);\n", snake_case_name, sname.len, sname.ptr);
    wt_format(w, "__attribute__((unused)) static void %s_free(struct %.*s val);\n", snake_case_name, sname.len, sname.ptr);
    free(snake_case_name);
}

static void write_struct_func(Writer *w, StructObject *obj, void *user_data) {
    Hashmap *layouts = user_data;
    // Retreive original TypeObject pointer from struct object pointer.
    TypeObject *t = (void *)((byte *)obj - offsetof(struct TypeObject, type));

    Layout *layout = hashmap_get(layouts, &(Layout){.type = t});
    assert(layout != NULL, "No layout found for struct");

    StringSlice sname = obj->name;
    char *snake_case_name = pascal_to_snake_case(sname);
    wt_format(w, "static int %s_serialize(struct %.*s val, byte *buf) {\n", snake_case_name, sname.len, sname.ptr);
    wt_format(w, "%*sbyte * base_buf = buf;\n", INDENT, "");
    write_type_serialization(w, "val", false, layout, (CurrentAlignment){.offset = 0, .align = t->align}, layouts, INDENT, 0, true);
    wt_format(w, "%*sreturn (int)(buf - base_buf);\n", INDENT, "");
    wt_format(w, "}\n");

    wt_format(w, "static int %s_deserialize(struct %.*s *val, const byte *buf) {\n", snake_case_name, sname.len, sname.ptr);
    wt_format(w, "%*sconst byte * base_buf = buf;\n", INDENT, "");
    write_type_deserialization(w, "val", true, layout, (CurrentAlignment){.offset = 0, .align = t->align}, layouts, INDENT, 0, true);
    wt_format(w, "%*sreturn (int)(buf - base_buf);\n", INDENT, "");
    wt_format(w, "}\n");

    wt_format(w, "static void %s_free(struct %.*s val) {", snake_case_name, sname.len, sname.ptr);
    BufferedWriter b = buffered_writer_init();
    int f = write_type_free((Writer*)&b, "val", t, layouts, INDENT, 0, true);
    if(f > 0) {
        wt_format(w, "\n%.*s}\n\n", b.buf.len, b.buf.data);
    } else {
        wt_format(w, " }\n\n");
    }
    buffered_writer_drop(b);

    free(snake_case_name);
}

void codegen_c(Writer *header, Writer *source, const char *name, Program *p) {
    char *uc_name = snake_case_to_screaming_snake_case((StringSlice){.ptr = name, .len = strlen(name)});
    wt_format(
        header,
        "// Generated file\n"
        "#ifndef %s_H\n"
        "#define %s_H\n"
        "#include <stdint.h>\n"
        "#include <stdlib.h>\n"
        "#include <stdbool.h>\n"
        "\n"
        "typedef unsigned char byte;\n"
        "typedef uint64_t MsgMagic;\n"
        "\n"
        "#define MSG_MAGIC_SIZE sizeof(MsgMagic)\n"
        "static const MsgMagic MSG_MAGIC_START = 0x%016lX;\n"
        "static const MsgMagic MSG_MAGIC_END = 0x%016lX;\n"
        "\n",
        uc_name,
        uc_name,
        MSG_MAGIC_START,
        MSG_MAGIC_END
    );
    free(uc_name);
    wt_format(
        source,
        "// Generated file\n"
        "#include \"%s.h\"\n"
        "#include <stdio.h>\n"
        "\n",
        name
    );

    define_structs(p, header, write_struct, NULL);
    define_structs(p, source, write_struct_func_decl, NULL);
    wt_format(source, "\n");
    define_structs(p, source, write_struct_func, p->layouts);

    for (size_t i = 0; i < p->messages.len; i++) {
        MessagesObject msgs = p->messages.data[i];

        wt_format(header, "// %.*s\n\n", msgs.name.len, msgs.name.ptr);
        wt_format(
            header,
            "typedef enum %.*sTag {\n%*s%.*sTagNone = 0,\n",
            msgs.name.len,
            msgs.name.ptr,
            INDENT,
            "",
            msgs.name.len,
            msgs.name.ptr
        );
        for (size_t j = 0; j < msgs.messages.len; j++) {
            wt_format(
                header,
                "%*s%.*sTag%.*s = %lu,\n",
                INDENT,
                "",
                msgs.name.len,
                msgs.name.ptr,
                msgs.messages.data[j].name.len,
                msgs.messages.data[j].name.ptr,
                j + 1
            );
        }
        wt_format(header, "} %.*sTag;\n\n", msgs.name.len, msgs.name.ptr);

        for (size_t j = 0; j < msgs.messages.len; j++) {
            MessageObject msg = msgs.messages.data[j];

            wt_format(header, "typedef struct %.*s%.*s {\n", msgs.name.len, msgs.name.ptr, msg.name.len, msg.name.ptr);
            wt_format(header, "%*s%.*sTag tag;\n", INDENT, "", msgs.name.len, msgs.name.ptr);

            for (size_t k = 0; k < msg.fields.len; k++) {
                Field f = msg.fields.data[k];
                write_field(header, f, NULL, 0, INDENT);
                wt_format(header, ";\n");
            }
            if (msg.attributes & Attr_versioned) {
                write_field(
                    header,
                    (Field){.name.ptr = "_version", .name.len = 8, .type = (TypeObject *)&PRIMITIF_u64},
                    NULL,
                    0,
                    INDENT
                );
                wt_format(header, ";\n");
            }

            wt_format(header, "} %.*s%.*s;\n\n", msgs.name.len, msgs.name.ptr, msg.name.len, msg.name.ptr);
        }

        wt_format(header, "typedef union %.*sMessage {\n", msgs.name.len, msgs.name.ptr);
        wt_format(header, "%*s%.*sTag tag;\n", INDENT, "", msgs.name.len, msgs.name.ptr);
        for (size_t j = 0; j < msgs.messages.len; j++) {
            MessageObject msg = msgs.messages.data[j];
            char *field = pascal_to_snake_case(msg.name);
            wt_format(header, "%*s%.*s%.*s %s;\n", INDENT, "", msgs.name.len, msgs.name.ptr, msg.name.len, msg.name.ptr, field);
            free(field);
        }
        wt_format(header, "} %.*sMessage;\n\n", msgs.name.len, msgs.name.ptr);

        char *name = pascal_to_snake_case(msgs.name);
        wt_format(
            header,
            "// Serialize the message msg to buffer dst of size len, returns the length of the serialized message, or -1 on "
            "error (buffer overflow)\n"
        );
        wt_format(header, "int msg_%s_serialize(byte *dst, size_t len, %.*sMessage *msg);\n", name, msgs.name.len, msgs.name.ptr);
        wt_format(
            header,
            "// Deserialize the message in the buffer src of size len into dst, return the length of the serialized message or "
            "-1 on error.\n"
        );
        wt_format(
            header,
            "int msg_%s_deserialize(const byte *src, size_t len, %.*sMessage *dst);\n\n",
            name,
            msgs.name.len,
            msgs.name.ptr
        );
        wt_format(
            header,
            "// Free the message (created by msg_%s_deserialize)\n"
            "void msg_%s_free(%.*sMessage *msg);\n",
            name,
            name,
            msgs.name.len,
            msgs.name.ptr
        );

        char *tag_type = msprintf("%.*sTag", msgs.name.len, msgs.name.ptr);
        PointerVec message_tos = vec_init();

        for (size_t j = 0; j < msgs.messages.len; j++) {
            MessageObject m = msgs.messages.data[j];
            TypeObject *to = malloc(sizeof(TypeObject));
            assert_alloc(to);
            {
                StructObject obj = {.name = m.name, .fields = vec_clone(&m.fields)};
                if (m.attributes & Attr_versioned) {
                    vec_push(&obj.fields, ((Field){.name.ptr = "_version", .name.len = 8, .type = (TypeObject *)&PRIMITIF_u64}));
                }
                to->type.struct_ = *(struct StructObject *)&obj;
                to->kind = TypeStruct;
                to->align = ALIGN_8;
            }
            Layout layout = type_layout(to);
            vec_push(&message_tos, to);

            hashmap_set(p->layouts, &layout);
        }

        {
            wt_format(
                source,
                "int msg_%s_serialize(byte *buf, size_t len, %.*sMessage *msg) {\n",
                name,
                msgs.name.len,
                msgs.name.ptr
            );

            wt_format(source, "%*sconst byte *base_buf = buf;\n", INDENT, "");
            wt_format(source, "%*sif(len < 2 * MSG_MAGIC_SIZE)\n", INDENT, "");
            wt_format(source, "%*sreturn -1;\n", INDENT * 2, "");
            wt_format(source, "%*s*(MsgMagic*)buf = MSG_MAGIC_START;\n", INDENT, "");
            wt_format(source, "%*sbuf += MSG_MAGIC_SIZE;\n", INDENT, "");
            wt_format(source, "%*sswitch(msg->tag) {\n", INDENT, "");
            wt_format(source, "%*scase %sNone:\n%*sbreak;\n", INDENT, "", tag_type, INDENT * 2, "");

            for (size_t j = 0; j < msgs.messages.len; j++) {
                MessageObject m = msgs.messages.data[j];
                TypeObject *mtype = message_tos.data[j];
                Layout *layout = hashmap_get(p->layouts, &(Layout){.type = mtype});
                assert(layout != NULL, "What ?");
                char *snake_case_name = pascal_to_snake_case(m.name);
                char *base = msprintf("msg->%s", snake_case_name);

                wt_format(source, "%*scase %s%.*s: {\n", INDENT, "", tag_type, m.name.len, m.name.ptr);
                wt_format(source, "%*s*(uint16_t *)buf = %s%.*s;\n", INDENT * 2, "", tag_type, m.name.len, m.name.ptr);
                if (m.attributes & Attr_versioned) {
                    wt_format(source, "%*smsg->%s._version = %luUL;\n", INDENT * 2, "", snake_case_name, msgs.version);
                }
                write_type_serialization(
                    source,
                    base,
                    false,
                    layout,
                    (CurrentAlignment){.align = ALIGN_8, .offset = 2},
                    p->layouts,
                    INDENT * 2,
                    0,
                    false
                );
                wt_format(source, "%*sbreak;\n%*s}\n", INDENT * 2, "", INDENT, "");

                free(base);
                free(snake_case_name);
            }
            wt_format(source, "%*s}\n", INDENT, "");
            wt_format(source, "%*s*(MsgMagic*)buf = MSG_MAGIC_END;\n", INDENT, "");
            wt_format(source, "%*sbuf += MSG_MAGIC_SIZE;\n", INDENT, "");
            wt_format(source, "%*sif(buf > base_buf + len)\n", INDENT, "");
            wt_format(source, "%*sreturn -1;\n", INDENT * 2, "");
            wt_format(source, "%*sreturn (int)(buf - base_buf);\n", INDENT, "");
            wt_format(source, "}\n");
        }

        {
            wt_format(
                source,
                "\nint msg_%s_deserialize(const byte *buf, size_t len, %.*sMessage *msg) {\n",
                name,
                msgs.name.len,
                msgs.name.ptr
            );

            wt_format(source, "%*sconst byte *base_buf = buf;\n", INDENT, "");
            wt_format(source, "%*sif(len < 2 * MSG_MAGIC_SIZE)\n", INDENT, "");
            wt_format(source, "%*sreturn -1;\n", INDENT * 2, "");
            wt_format(source, "%*sif(*(MsgMagic*)buf != MSG_MAGIC_START)\n", INDENT, "");
            wt_format(source, "%*sreturn -1;\n", INDENT * 2, "");
            wt_format(source, "%*sbuf += MSG_MAGIC_SIZE;\n", INDENT, "");
            wt_format(source, "%*s%s tag = *(uint16_t*)buf;\n", INDENT, "", tag_type);
            wt_format(source, "%*sswitch(tag) {\n", INDENT, "");
            wt_format(source, "%*scase %sNone:\n%*sbreak;\n", INDENT, "", tag_type, INDENT * 2, "");

            for (size_t j = 0; j < msgs.messages.len; j++) {
                MessageObject m = msgs.messages.data[j];
                TypeObject *mtype = message_tos.data[j];
                Layout *layout = hashmap_get(p->layouts, &(Layout){.type = mtype});
                assert(layout != NULL, "What ?");
                char *snake_case_name = pascal_to_snake_case(m.name);
                char *base = msprintf("msg->%s", snake_case_name);

                wt_format(source, "%*scase %s%.*s: {\n", INDENT, "", tag_type, m.name.len, m.name.ptr);
                wt_format(source, "%*smsg->tag = %s%.*s;\n", INDENT * 2, "", tag_type, m.name.len, m.name.ptr);
                write_type_deserialization(
                    source,
                    base,
                    false,
                    layout,
                    (CurrentAlignment){.align = ALIGN_8, .offset = 2},
                    p->layouts,
                    INDENT * 2,
                    0,
                    false
                );
                if (m.attributes & Attr_versioned) {
                    wt_format(source, "%*sif(msg->%s._version != %luUL) {\n", INDENT * 2, "", snake_case_name, msgs.version);
                    wt_format(source, "%*sprintf(\"Mismatched version: peers aren't the same version", INDENT * 3, "");
                    wt_format(source, ", expected %lu got %%lu.\\n\", msg->%s._version);\n", msgs.version, snake_case_name);
                    wt_format(source, "%*smsg_%s_free(msg);\n", INDENT * 3, "", name);
                    wt_format(source, "%*sreturn -1;\n", INDENT * 3, "");
                    wt_format(source, "%*s}\n", INDENT * 2, "");
                }
                wt_format(source, "%*sbreak;\n%*s}\n", INDENT * 2, "", INDENT, "");

                free(base);
                free(snake_case_name);
            }
            wt_format(source, "%*s}\n", INDENT, "");
            wt_format(source, "%*sif(*(MsgMagic*)buf != MSG_MAGIC_END) {\n", INDENT, "");
            wt_format(source, "%*smsg_%s_free(msg);\n", INDENT * 2, "", name);
            wt_format(source, "%*sreturn -1;\n", INDENT * 2, "");
            wt_format(source, "%*s}\n", INDENT, "");
            wt_format(source, "%*sbuf += MSG_MAGIC_SIZE;\n", INDENT, "");
            wt_format(source, "%*sif(buf > base_buf + len) {\n", INDENT, "");
            wt_format(source, "%*smsg_%s_free(msg);\n", INDENT * 2, "", name);
            wt_format(source, "%*sreturn -1;\n", INDENT * 2, "");
            wt_format(source, "%*s}\n", INDENT, "");
            wt_format(source, "%*sreturn (int)(buf - base_buf);\n", INDENT, "");
            wt_format(source, "}\n");
        }

        {
            wt_format(source, "\nvoid msg_%s_free(%.*sMessage *msg) {\n", name, msgs.name.len, msgs.name.ptr);

            wt_format(source, "%*sswitch(msg->tag) {\n", INDENT, "");
            wt_format(source, "%*scase %sNone:\n%*sbreak;\n", INDENT, "", tag_type, INDENT * 2, "");

            for (size_t j = 0; j < msgs.messages.len; j++) {
                MessageObject m = msgs.messages.data[j];
                TypeObject *mtype = message_tos.data[j];

                char *snake_case_name = pascal_to_snake_case(m.name);
                char *base = msprintf("msg->%s", snake_case_name);

                wt_format(source, "%*scase %s%.*s: {\n", INDENT, "", tag_type, m.name.len, m.name.ptr);
                write_type_free(source, base, mtype, p->layouts, INDENT * 2, 0, false);
                wt_format(source, "%*sbreak;\n%*s}\n", INDENT * 2, "", INDENT, "");

                free(base);
                free(snake_case_name);
            }
            wt_format(source, "%*s}\n", INDENT, "");
            wt_format(source, "}\n");
        }

        for (size_t j = 0; j < message_tos.len; j++) {
            TypeObject *to = message_tos.data[j];
            StructObject *s = (StructObject *)&to->type.struct_;

            vec_drop(s->fields);
            free(to);
        }

        vec_drop(message_tos);

        free(tag_type);
        free(name);
    }

    wt_format(header, "#endif\n");
}

typedef struct {
    uint16_t field_count;
    char *a[4];
} AA;
