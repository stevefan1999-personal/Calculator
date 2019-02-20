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

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define M_PI 3.14159265358979323846

#define AST_DEBUG 1

KHASH_MAP_INIT_STR(var, double);
KHASH_INIT(set, struct node *, char, 0, kh_int_hash_func, kh_int_hash_equal)

typedef struct {
  tgc_t gc;
  khash_t(var) *h;
  char halt: 1;
} parser_state_t;

static parser_state_t state;

void *calc_alloc(size_t bytes, void *yyscanner) {
  if (yyscanner) {
    parser_state_t *state = calc_get_extra(yyscanner);
    return tgc_alloc(&state->gc, bytes);
  }
  return malloc(bytes);
}

void *calc_realloc(void *ptr, size_t bytes, void *yyscanner) {
  if (yyscanner) {
    parser_state_t *state = calc_get_extra(yyscanner);
    return tgc_realloc(&state->gc, ptr, bytes);
  }
  return realloc(ptr, bytes);
}

void calc_free(void *ptr, void *yyscanner) {
  if (yyscanner) {
    parser_state_t *state = calc_get_extra(yyscanner);
    tgc_free(&state->gc, ptr);
  } else {
    free(ptr);
  }
}

void calc_halt(void *yyscanner) {
  ((parser_state_t *) calc_get_extra(yyscanner))->halt = 1;
}

const char *calc_strdup(const char *str, void *yyscanner) {
  if (yyscanner) {
    size_t len = strlen(str);
    return memcpy(calc_alloc(len, yyscanner), str, len);
  } else {
    return strdup(str);
  }
}

double calc_get_var(const char *name, void *yyscanner);

void calc_set_var(const char *name, double val, void *yyscanner);

double calc_eval_ast(struct node *ast, size_t level);

double calc_get_var(const char *name, void *yyscanner) {
  khash_t(var) *h = ((parser_state_t *) calc_get_extra(yyscanner))->h;

  for (khiter_t k = kh_get(var, h, name);;) {
    double val = k != kh_end(h) ? kh_val(h, k) : NAN;
    return val;
  }
}

void calc_set_var(const char *name, double val, void *yyscanner) {
  khash_t(var) *h = ((parser_state_t *) calc_get_extra(yyscanner))->h;
  int ret;
  // BUG: reference-after-free due to khash not storing hashes but key pointers
  // for C string it is being treated as mere const char * only
  khiter_t k = kh_put(var, h, calc_strdup(name, NULL), &ret);
  kh_value(h, k) = val;
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

  double (*fn)(double);
} fp[] = {
  {"sin",    sin},
  {"cos",    cos},
  {"tan",    tan},
  {"asin",   asin},
  {"acos",   acos},
  {"atan",   atan},
  {"deg",    deg_to_rad},
  {"rad",    rad_to_deg},
  {"abs",    fabs},
  {"sqrt",   sqrt},
  {"log2",   log2},
  {"log",    log},
  {"log10",  log10},
  {"exp",    exp},
  {"cbrt",   cbrt},
  {"floor",  _floor},
  {"ceil",   _ceil},
  {"lgamma", lgamma},
  {"gamma",  tgamma},
  {"round",  round},
  {"fac",    factorial}
};

int get_function_idx(const char *op) {
  for (int i = 0; i < sizeof(fp) / sizeof(fp[0]); i++) {
    if (streql(fp[i].name, op)) {
      return i;
    }
  }
  return -1;
}

void free_ast(struct node *ast) {
  printf("freeing the ast @ %p\n", ast);
}

struct node *make_node(void *yyscanner, const char *op, struct node *left, struct node *right) {
  struct node *ast = tgc_alloc_opt(calc_get_extra(yyscanner), sizeof(struct node), 0, free_ast);
  ast->op = op;
  ast->left = left;
  ast->right = right;
  ast->yyscanner = yyscanner;

  if (streql(ast->op, "identifier")) {
    int idx = get_function_idx((const char *) ast->left);
    if (idx != -1) {
      ast->op = "function identifier";
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
    return make_node(yyscanner, ast->op,
                     (struct node *) memcpy(calc_alloc(sizeof(double), yyscanner), ast->left, sizeof(double)), NULL);
  }
  return make_node(yyscanner, ast->op, ast->left, ast->right);
}

void eval_function_list(struct node *args, size_t level, kvec_t(double) *stack) {
  if (streql(args->op, ",")) {
    if (!args->left && !args->right) return;
    if (args->left) eval_function_list(args->left, level, stack);
    if (args->right) eval_function_list(args->right, level, stack);
  } else {
    kv_push(double, *stack, calc_eval_ast(args, level + 1));
  }
}

double calc_eval_ast(struct node *ast, size_t level) {
#if AST_DEBUG
  if (ast->right == NULL) {
    if (streql(ast->op, "+") || streql(ast->op, "-") || streql(ast->op, "!") || streql(ast->op, "!!")) {
      printf("%*sunary %s\n", level * 2, "", ast->op);
    } else if (streql(ast->op, "number")) {
      printf("%*s%s %g\n", level * 2, "", ast->op, *(double *) ast->left);
    } else if (streql(ast->op, "string")) {
      printf("%*s%s %s\n", level * 2, "", ast->op, (const char *) ast->left);
    } else if (streql(ast->op, "identifier") || streql(ast->op, "function identifier")) {
      printf("%*s%s %s %g\n", level * 2, "", ast->op, (const char *) ast->left,
             calc_get_var((const char *) ast->left, ast->yyscanner));
    }
  } else {
    if (streql(ast->op, "function")) {
      printf("%*s%s %s\n", level * 2, "", ast->op, (const char *) ast->left->left);
    } else {
      printf("%*sbinary %s\n", level * 2, "", ast->op);
    }
  }
#endif

#define HANDLE(ast, name) if (streql((ast)->op, name))

  if (ast->right == NULL) {
    HANDLE(ast, "+") return +calc_eval_ast(ast->left, level + 1);
    HANDLE(ast, "-") return -calc_eval_ast(ast->left, level + 1);
    HANDLE(ast, "!") return !calc_eval_ast(ast->left, level + 1);
    HANDLE(ast, "!!") return factorial(calc_eval_ast(ast->left, level + 1));
    HANDLE(ast, "number") return *(double *) ast->left;
    HANDLE(ast, "identifier") return calc_get_var((const char *) ast->left, ast->yyscanner);
  } else {
#define GENERATE_TRANSFORMER_BINARY(ast, name) \
    HANDLE(ast, #name) { \
      double l = calc_eval_ast(ast->left, level + 1); \
      double r = calc_eval_ast(ast->right, level + 1); \
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
      double l = calc_eval_ast(ast->left, level + 1);
      double r = calc_eval_ast(ast->right, level + 1);
      return fabs(l - r) <= DBL_EPSILON * max(fabs(l), fabs(r));
    }
    HANDLE(ast, "^^") {
      double l = calc_eval_ast(ast->left, level + 1);
      double r = calc_eval_ast(ast->right, level + 1);
      return pow(l, r);
    }
    HANDLE(ast, ",") { // well, this is a tricky one
      double l = calc_eval_ast(ast->left, level + 1);
      double r = calc_eval_ast(ast->right, level + 1);
      // return l, r;
      return r;
    }
    HANDLE(ast, "%") {
      double l = calc_eval_ast(ast->left, level + 1);
      double r = calc_eval_ast(ast->right, level + 1);
      return fmod(l, r);
    }
    HANDLE(ast, "=") {
      double l = calc_eval_ast(ast->left, level + 1);
      double r = calc_eval_ast(ast->right, level + 1);
      calc_set_var((const char *) ast->left->left, r, ast->yyscanner);
      return calc_get_var((const char *) ast->left->left, ast->yyscanner);
    }
    HANDLE(ast, "function") {
      const char *name = (const char *) ast->left->left;
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

int main(void) {
  char stk;
  tgc_start(&state.gc, &stk);

  state.h = kh_init(var);
  printf("Simple calculator:\n");

  yyscan_t yyscanner;
  calc_lex_init(&yyscanner);
  calc_set_debug(AST_DEBUG, yyscanner);
  calc_set_extra(&state.gc, yyscanner);

  for (;; fflush(stdin)) {
    calc_restart(stdin, yyscanner);
    calc_parse(yyscanner);
    if (((parser_state_t *) calc_get_extra(yyscanner))->halt) break;
  }

  calc_lex_destroy(yyscanner);
  kh_destroy(var, state.h);
  getchar();
  tgc_stop(&state.gc);
  return 0;
}