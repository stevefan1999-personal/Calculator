#include <math.h>
#include <stdio.h>
#include <float.h>
#include "klib/khash.h"
#include "klib/kvec.h"
#include "tgc/tgc.h"

#define YYPOLLUTE_NAMESPACE
#include "calc.parse.h"
#include "calc.lex.h"

#undef max
#undef min
#undef M_PI

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define M_PI 3.14159265358979323846

#define AST_DEBUG 1

static tgc_t gc;

void *calc_alloc(size_t bytes, void *yyscanner) {
    return yyscanner ? tgc_alloc(calc_get_extra(yyscanner), bytes) : malloc(bytes);
}

void *calc_realloc(void *ptr, size_t bytes, void *yyscanner) {
    return yyscanner ? tgc_realloc(calc_get_extra(yyscanner), ptr, bytes) : realloc(ptr, bytes);
}

void calc_free(void *ptr, void *yyscanner) {
    yyscanner ? tgc_free(calc_get_extra(yyscanner), ptr) : free(ptr);
}

double get_var(const char *name);
void set_var(const char *name, double val);

void free_ast(void *yyscanner, struct node *ast);
double eval_ast(struct node *ast, size_t level);

KHASH_MAP_INIT_STR(var, double);
KHASH_INIT(set, struct node *, char, 0, kh_int_hash_func, kh_int_hash_equal)

static khash_t(var) *h = NULL;

double get_var(const char *name) {
    for (khiter_t k = kh_get(var, h, name); ;) {
        double val = k != kh_end(h) ? kh_val(h, k) : NAN;
        return val;
    }
}

void set_var(const char *name, double val) {
    int ret;
    // BUG: reference-after-free due to khash not storing hashes but key pointers
    // for C string it is being treated as mere const char * only
    kh_value(h, kh_put(var, h, strdup(name), &ret)) = val;
}

int streql(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

double rad_to_deg(double x) {
    return x * 180.0f / M_PI;
}

double deg_to_rad(double x) {
    return x * M_PI / 180.0f;
}

double factorial(double x) {
    double ret = tgamma(x + 1);
    return fmod(x, 1) == 0 ? round(ret) : ret;
}

double _floor(double x) {
    return floor(x);
}

double _ceil(double x) {
    return ceil(x);
}

const struct {
    const char *name;
    double(*fn)(double);
} fp[] = {
    {"sin", sin},
    {"cos", cos},
    {"tan", tan},
    {"asin", asin},
    {"acos", acos},
    {"atan", atan},
    {"deg", deg_to_rad},
    {"rad", rad_to_deg},
    {"abs", fabs},
    {"sqrt", sqrt},
    {"log2", log2},
    {"log", log},
    {"log10", log10},
    {"exp", exp},
    {"cbrt", cbrt},
    {"floor", _floor},
    {"ceil", _ceil},
    {"lgamma", lgamma},
    {"gamma", tgamma},
    {"round", round},
    {"fac", factorial}
};

int get_function_idx(const char *op) {
    for (int i = 0; i < sizeof(fp) / sizeof(fp[0]); i++) {
        if (streql(fp[i].name, op)) {
            return i;
        }
    }
    return -1;
}

struct node *make_node(void *yyscanner, const char *op, struct node *left, struct node *right) {
    struct node *ast = tgc_alloc(calc_get_extra(yyscanner), sizeof(struct node));
    ast->op = op;
    ast->left = left;
    ast->right = right;

    if (streql(ast->op, "identifier")) {
        int idx = get_function_idx((const char *)ast->left);
        if (idx != -1) {
            ast->op = "function identifier";
            // free(ast->left);
            ast->left = (struct node *) fp[idx].name;
        }
    }

    return ast;
}

struct node *clone_node(void *yyscanner, struct node *ast) {
    if (streql(ast->op, "identifier")) {
        const char *iden = (const char *) ast->left;
        size_t len = strlen(iden);
        return make_node(yyscanner, ast->op, (struct node *) memcpy(calc_alloc(len, yyscanner), ast->left, len), NULL);
    }
    if (streql(ast->op, "number")) {
        return make_node(yyscanner, ast->op, (struct node *) memcpy(calc_alloc(sizeof(double), yyscanner), ast->left, sizeof(double)), NULL);
    }
    return make_node(yyscanner, ast->op, ast->left, ast->right);
}

void free_ast_dfs(void *yyscanner, struct node *ast, khash_t(set) *walked) {
    int ret;
    if (ast != NULL) {
#if AST_DEBUG
        printf("freeing the ast @ %p\n", ast);
#endif

        if (streql(ast->op, "function identifier")) {
            ;
        } else if (streql(ast->op, "number") || streql(ast->op, "identifier") || streql(ast->op, "string")) {
            if (kh_get(set, walked, ast->left) == kh_end(walked)) {
                // free(ast->left);
                kh_val(walked, kh_put(set, walked, ast->left, &ret));
            }

        } else {
            if (kh_get(set, walked, ast->left) == kh_end(walked)) {
                free_ast_dfs(yyscanner, ast->left, walked);
            }

            if (kh_get(set, walked, ast->right) == kh_end(walked)) {
                free_ast_dfs(yyscanner, ast->right, walked);
            }
        }

        if (kh_get(set, walked, ast) == kh_end(walked)) {
            // free(ast);
            kh_val(walked, kh_put(set, walked, ast, &ret));
        }

    }
}

void free_ast(void *yyscanner, struct node *ast) {
    khash_t(set) *set = kh_init(set);
    free_ast_dfs(yyscanner, ast, set);
    kh_destroy(set, set);
}
 
void eval_function_list(struct node *args, size_t level, kvec_t(double) *stack) {
    if (streql(args->op, ",")) {
        if (!args->left && !args->right) return;
        if (args->left) eval_function_list(args->left, level, stack);
        if (args->right) eval_function_list(args->right, level, stack);
    } else {
        kv_push(double, *stack, eval_ast(args, level + 1));
    }
}

void print_graphviz(struct node *ast);

double eval_ast(struct node *ast, size_t level) {
#if AST_DEBUG
    if (ast->right == NULL) {
        if (streql(ast->op, "+") || streql(ast->op, "-") || streql(ast->op, "!") || streql(ast->op, "!!")) {
            printf("%*sunary %s\n", level * 2, "", ast->op);
        } else if (streql(ast->op, "number")) {
            printf("%*s%s %g\n", level * 2, "", ast->op, *(double *)ast->left);
        } else if (streql(ast->op, "string")) {
            printf("%*s%s %s\n", level * 2, "", ast->op, (const char *)ast->left);
        } else if (streql(ast->op, "identifier") || streql(ast->op, "function identifier")) {
            printf("%*s%s %s %g\n", level * 2, "", ast->op, (const char *) ast->left, get_var((const char *)ast->left));
        }
    } else {
        if (streql(ast->op, "function")) {
            printf("%*s%s %s\n", level * 2, "", ast->op, (const char *)ast->left->left);
        } else {
            printf("%*sbinary %s\n", level * 2, "", ast->op);
        }
    }
#endif

#define HANDLE(ast, name) if (streql((ast)->op, name))

    if (ast->right == NULL) {
        HANDLE(ast, "+") return +eval_ast(ast->left, level + 1);
        HANDLE(ast, "-")  return -eval_ast(ast->left, level + 1);
        HANDLE(ast, "!")  return !eval_ast(ast->left, level + 1);
        HANDLE(ast, "!!") return factorial(eval_ast(ast->left, level + 1));
        HANDLE(ast, "number")  return *(double *)ast->left;
        HANDLE(ast, "identifier") return get_var((const char *)ast->left);
    } else {
#define GENERATE_TRANSFORMER_BINARY(ast, name) \
		HANDLE(ast, #name) { \
			double l = eval_ast(ast->left, level + 1); \
			double r = eval_ast(ast->right, level + 1); \
			return l name r; \
		} do; while(0)

        GENERATE_TRANSFORMER_BINARY(ast, +);
        GENERATE_TRANSFORMER_BINARY(ast, -);
        GENERATE_TRANSFORMER_BINARY(ast, *);
        GENERATE_TRANSFORMER_BINARY(ast, /);
        GENERATE_TRANSFORMER_BINARY(ast, >);
        GENERATE_TRANSFORMER_BINARY(ast, <);
        GENERATE_TRANSFORMER_BINARY(ast, &&);
        GENERATE_TRANSFORMER_BINARY(ast, ||);

        HANDLE(ast, "==") {
            double l = eval_ast(ast->left, level + 1);
            double r = eval_ast(ast->right, level + 1);
            return fabs(l - r) <= DBL_EPSILON * max(fabs(l), fabs(r));
        }
        HANDLE(ast, "^^") {
            double l = eval_ast(ast->left, level + 1);
            double r = eval_ast(ast->right, level + 1);
            return pow(l, r);
        }
        HANDLE(ast, ",") { // well, this is a tricky one
            double l = eval_ast(ast->left, level + 1);
            double r = eval_ast(ast->right, level + 1);
            // return l, r;
            return r;
        }
        HANDLE(ast, "%") {
            double l = eval_ast(ast->left, level + 1);
            double r = eval_ast(ast->right, level + 1);
            return fmod(l, r);
        }
        HANDLE(ast, "=") {
            double l = eval_ast(ast->left, level + 1);
            double r = eval_ast(ast->right, level + 1);
            set_var((const char *)ast->left->left, r);
            return get_var((const char *)ast->left->left);
        }
        HANDLE(ast, "function") {
            const char *name = (const char *)ast->left->left;
            const int idx = get_function_idx(name);
            if (idx != -1) {
                kvec_t(double) args;
                kv_init(args);
                eval_function_list(ast->right, level, &args);
                double ret = fp[idx].fn(kv_a(double, args, 0));
                kv_destroy(args);
                return ret;
            }
        }
    }

    return NAN;
}

typedef enum {
  PRE,
  IN,
  POST
} order_t;

void print_dot(struct node *ast, int node, int parent, order_t order) {
#define HANDLE(ast, name) if (streql((ast)->op, name))
    if (ast->right == NULL) {
#define GENERATE_TRANSFORMER_UNARY(ast, name) \
  HANDLE(ast, #name) { \
    if (order == PRE) { \
      printf(" %i [label=\"%s\"];\n", node, #name); \
      printf(" %i -> %i;\n", parent, node); \
    } \
    print_dot(ast->left, 2*node, node, order); \
    if (order == POST) { \
      printf(" %i [label=\"%s\"];\n", node, #name); \
      printf(" %i -> %i;\n", parent, node); \
    } \
  }
        GENERATE_TRANSFORMER_UNARY(ast, +);
        GENERATE_TRANSFORMER_UNARY(ast, -);

        HANDLE(ast, "number") {
          printf(" %i [label=\"%g\"];\n", node, *(double *)ast->left);
          printf(" %i -> %i;\n", parent, node);
        }
        HANDLE(ast, "identifier") {
          printf(" %i [label=\"%s\"];\n", node, (const char *) ast->left);
          printf(" %i -> %i;\n", parent, node);
        }

    } else {
#define GENERATE_TRANSFORMER_BINARY(ast, name) \
		HANDLE(ast, #name) { \
      if (order == PRE) { \
        printf(" %i [label=\"%s\"];\n", node, #name); \
        printf(" %i -> %i;\n", parent, node); \
      } \
      if (ast->left) { \
        print_dot(ast->left, 2*node, node, order); \
      } \
      if (order == IN) { \
        printf(" %i [label=\"%s\"];\n", node, #name); \
        printf(" %i -> %i;\n", parent, node); \
      } \
      if (ast->right) { \
        print_dot(ast->right, 2*node+1, node, order); \
      } \
      if (order == POST) { \
        printf(" %i [label=\"%s\"];\n", node, #name); \
        printf(" %i -> %i;\n", parent, node); \
      } \
		} do; while(0)

        GENERATE_TRANSFORMER_BINARY(ast, +);
        GENERATE_TRANSFORMER_BINARY(ast, -);
        GENERATE_TRANSFORMER_BINARY(ast, *);
        GENERATE_TRANSFORMER_BINARY(ast, /);
        GENERATE_TRANSFORMER_BINARY(ast, >);
        GENERATE_TRANSFORMER_BINARY(ast, <);
        GENERATE_TRANSFORMER_BINARY(ast, &&);
        GENERATE_TRANSFORMER_BINARY(ast, ||);
    }
}

void print_graphviz(struct node *ast) {
  printf("digraph tree {\n");
  printf(" rank=same;\n");
  printf(" node [fontname=\"Arial\", shape=\"circle\"];\n");
  print_dot(ast, 1, 0, IN);
  printf("}\n");
}

int main(void) {
    char stk;
    tgc_start(&gc, &stk);

    if (h == NULL) h = kh_init(var);
    printf("Simple calculator:\n");

    yyscan_t yyscanner;
    calc_lex_init(&yyscanner);
    calc_set_debug(AST_DEBUG, yyscanner);
    calc_set_extra(&gc, yyscanner);
    
    for (;; fflush(stdin)) {
        calc_restart(stdin, yyscanner);
        calc_parse(yyscanner);
        if ((int)calc_get_extra(yyscanner) == 1) break;
    }
    
    calc_lex_destroy(yyscanner);
    kh_destroy(var, h);
    getchar();
    tgc_stop(&gc);
    return 0;
}