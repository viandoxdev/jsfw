#include "ast.h"

#include "arena_allocator.h"
#include "vector.h"

AstContext ast_init() {
    return (AstContext){
        .root = NULL,
        .alloc = arena_init(),
    };
}

static void ast_node_drop(AstNode *node) {
    switch (node->tag) {
    case ATStruct:
        vec_drop(node->struct_.fields);
        break;
    case ATMessage:
        vec_drop(node->message.fields);
        break;
    case ATMessages:
        for (size_t i = 0; i < node->messages.children.len; i++) {
            ast_node_drop((AstNode *)&node->messages.children.data[i]);
        }
        vec_drop(node->messages.children);
        break;
    case ATItems:
        for (size_t i = 0; i < node->items.items.len; i++) {
            ast_node_drop((AstNode *)&node->items.items.data[i]);
        }
        vec_drop(node->items.items);
        break;
    default:
        break;
    }
}

void ast_drop(AstContext ctx) {
    if (ctx.root != NULL) {
        ast_node_drop(ctx.root);
    }
    arena_drop(ctx.alloc);
}

static void print(AstNode *node, uint32_t indent) {
    const uint32_t I = 4;
    switch (node->tag) {
    case ATNumber:
        fprintf(stderr, "%*sAstNumber(%.*s)\n", indent, "", node->number.token.span.len, node->number.token.lexeme);
        break;
    case ATIdent:
        fprintf(stderr, "%*sAstIdent(%.*s)\n", indent, "", node->ident.token.span.len, node->ident.token.lexeme);
        break;
    case ATVersion:
        fprintf(stderr, "%*sAstVersion:\n", indent, "");
        print((AstNode *)&node->version.version, indent + I);
        break;
    case ATNoSize:
        fprintf(stderr, "%*sAstSize(none)\n", indent, "");
        break;
    case ATMaxSize:
        fprintf(stderr, "%*sAstSize(max):\n", indent, "");
        print((AstNode *)&node->size.value, indent + I);
        break;
    case ATFixedSize:
        fprintf(stderr, "%*sAstSize(fixed):\n", indent, "");
        print((AstNode *)&node->size.value, indent + I);
        break;
    case ATHeapArray:
        fprintf(stderr, "%*sAstArray(heap):\n", indent, "");
        print((AstNode *)node->array.type, indent + I);
        print((AstNode *)&node->array.size, indent + I);
        break;
    case ATFieldArray:
        fprintf(stderr, "%*sAstArray(field):\n", indent, "");
        print((AstNode *)node->array.type, indent + I);
        print((AstNode *)&node->array.size, indent + I);
        break;
    case ATField:
        fprintf(stderr, "%*sAstField(%.*s):\n", indent, "", node->field.name.span.len, node->field.name.lexeme);
        print((AstNode *)&node->field.type, indent + I);
        break;
    case ATStruct:
        fprintf(stderr, "%*sAstStruct(%.*s):\n", indent, "", node->struct_.ident.span.len, node->struct_.ident.lexeme);
        for (size_t i = 0; i < node->struct_.fields.len; i++) {
            print((AstNode *)&node->struct_.fields.data[i], indent + I);
        }
        break;
    case ATMessage:
        fprintf(stderr, "%*sAstMessage(%.*s):\n", indent, "", node->message.ident.span.len, node->message.ident.lexeme);
        for (size_t i = 0; i < node->message.fields.len; i++) {
            print((AstNode *)&node->message.fields.data[i], indent + I);
        }
        break;
    case ATAttribute:
        fprintf(stderr, "%*sAstAttribute(%.*s)\n", indent, "", node->attribute.ident.span.len, node->attribute.ident.lexeme);
        break;
    case ATMessages:
        fprintf(stderr, "%*sAstMessages(%.*s):\n", indent, "", node->messages.name.span.len, node->messages.name.lexeme);
        for (size_t i = 0; i < node->messages.children.len; i++) {
            print((AstNode *)&node->messages.children.data[i], indent + I);
        }
        break;
    case ATTypeDecl:
        fprintf(stderr, "%*sAstTypeDecl(%.*s):\n", indent, "", node->type_decl.name.span.len, node->type_decl.name.lexeme);
        print((AstNode *)&node->type_decl.value, indent + I);
        break;
    case ATConstant:
        fprintf(stderr, "%*sAstConstant(%.*s):\n", indent, "", node->constant.name.span.len, node->constant.name.lexeme);
        print((AstNode *)&node->constant.value, indent + I);
        break;
    case ATItems:
        fprintf(stderr, "%*sAstItems:\n", indent, "");
        for (size_t i = 0; i < node->items.items.len; i++) {
            print((AstNode *)&node->items.items.data[i], indent + I);
        }
        break;
    }
}

void ast_print(AstNode *node) { print(node, 0); }
