#define _GNU_SOURCE 1

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>

#include <string.h>

static GScannerConfig parser_config =
{
 .cset_skip_characters = ("\t\r\n"),
 .cset_identifier_first = (G_CSET_a_2_z G_CSET_A_2_Z),
 .cset_identifier_nth = (G_CSET_a_2_z G_CSET_A_2_Z G_CSET_DIGITS "-"),
 .cpair_comment_single = ("#\n"),
 .case_sensitive = TRUE,
 .skip_comment_multi = TRUE,
 .skip_comment_single = TRUE,
 .scan_comment_multi = FALSE,
 .scan_identifier = TRUE,
 .scan_identifier_1char = FALSE,
 .scan_identifier_NULL = FALSE,
 .scan_symbols = TRUE,
 .scan_binary = FALSE,
 .scan_octal = FALSE,
 .scan_float = TRUE,
 .scan_hex = FALSE,
 .scan_hex_dollar = FALSE,
 .scan_string_sq = TRUE,
 .scan_string_dq = TRUE,
 .numbers_2_int = TRUE,
 .int_2_float = FALSE,
 .identifier_2_string = FALSE,
 .char_2_token = TRUE,
 .symbol_2_token = TRUE,
 .scope_0_fallback = FALSE,
};

enum
{
  QL_TOKEN_INVALID = G_TOKEN_LAST,
  QL_TOKEN_LAST
};

enum
{
  QL_SCOPE_DEFAULT = 0,
};

#define BOLT_TYPE_QUERY_PARSER bolt_query_parser_get_type ()
G_DECLARE_FINAL_TYPE (BoltQueryParser, bolt_query_parser, BOLT, QUERY_PARSER, GObject)

struct _BoltQueryParser
{
  GObject parent;

  GScanner *scanner;
  GError   *error;
};

enum {
  PROP_PARSER_0,
  PROP_PARSER_ERROR,
  PROP_PARSER_LAST
};

static GParamSpec *parser_props[PROP_PARSER_LAST] = {NULL, };

G_DEFINE_TYPE (BoltQueryParser, bolt_query_parser, G_TYPE_OBJECT)

static void
bolt_device_finalize (GObject *object)
{
  BoltQueryParser *parser = BOLT_QUERY_PARSER (object);

  g_clear_pointer (&parser->scanner, g_scanner_destroy);
  g_clear_error (&parser->error);

  G_OBJECT_CLASS (bolt_query_parser_parent_class)->finalize (object);
}

static void
bolt_query_parser_init (BoltQueryParser *parser)
{
  parser->scanner = g_scanner_new (&parser_config);
}

static void
bolt_query_parser_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  BoltQueryParser *parser = BOLT_QUERY_PARSER (object);

  switch (prop_id)
    {
    case PROP_PARSER_ERROR:
      g_value_set_boxed (value, parser->error);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_query_parser_class_init (BoltQueryParserClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_device_finalize;
  gobject_class->get_property = bolt_query_parser_get_property;

  parser_props[PROP_PARSER_ERROR] =
    g_param_spec_boxed ("error", "Error", NULL,
                        G_TYPE_ERROR,
                        G_PARAM_READABLE |
                        G_PARAM_STATIC_NICK |
                        G_PARAM_STATIC_BLURB);

  g_object_class_install_properties (gobject_class,
                                     PROP_PARSER_LAST,
                                     parser_props);


}

/* AST */

typedef struct Expr Expr;
typedef struct ExprClass ExprClass;

typedef struct Condition Condition;
typedef struct Unary Unary;
typedef struct Binary Binary;

typedef void (*ExprStringify) (Expr *expr, GString *out);
typedef void (*ExprFree) (Expr *expr);

struct ExprClass {
  ExprStringify stringify;
  ExprFree      free;
};

struct Expr {
  ExprClass *klass;
};

struct Condition {
  Expr expr;

  char *field;
  GValue val;
};

struct Unary {
  Expr expr;

  int   op;
  Expr *rhs;
};

struct Binary {
  Expr expr;

  int   op;
  Expr *lhs;
  Expr *rhs;
};

/*  */
static inline void
expression_stringify (Expr *exp, GString *out)
{
  exp->klass->stringify (exp, out);
}

static inline void
expression_free (Expr *exp)
{
  if (exp == NULL)
    return;

  exp->klass->free (exp);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Expr, expression_free)

/*  */
static void
condition_stringify (Expr *exp, GString *out)
{
  g_autofree char *tmp = NULL;
  struct Condition *c;

  c = (struct Condition *) exp;

  tmp = g_strdup_value_contents (&c->val);
  g_string_append_printf (out, "%s:%s", c->field, tmp);
}

static void
condition_free (Expr *exp)
{
  struct Condition *c = (struct Condition *) exp;

  g_free (c->field);
  g_value_unset (&c->val);

  g_slice_free (Condition, c);
}

static ExprClass ConditionClass =
{
 .stringify = condition_stringify,
 .free = condition_free,
};

static Condition *
condition_new (void)
{
  Condition *c;

  c = g_slice_new0 (Condition);
  c->expr.klass = &ConditionClass;

  return c;
}

static inline void
condition_cleanup (Condition *c)
{
  condition_free ((Expr *) c);
}


G_DEFINE_AUTOPTR_CLEANUP_FUNC(Condition, condition_cleanup)

/*  */
static void
unary_stringify (Expr *exp, GString *out)
{
  struct Unary *u = (struct Unary *) exp;

  g_string_append_printf (out, "%c(", u->op);
  expression_stringify (u->rhs, out);
  g_string_append_printf (out, ")");
}

static void
unary_free (Expr *exp)
{
  Unary *u;

  u = (Unary *) exp;

  expression_free (u->rhs);
  g_slice_free (struct Unary, u);
}

static ExprClass UnaryClass =
{
 .stringify = unary_stringify,
 .free = unary_free,
};

static Unary *
unary_new (int op)
{
  Unary *u;

  u = g_slice_new0 (Unary);
  u->expr.klass = &UnaryClass;
  u->op = op;

  return u;
}

/* */
static void
binary_stringify (Expr *exp, GString *out)
{
  Binary *b = (Binary *) exp;

  g_string_append_printf (out, "(");
  expression_stringify (b->lhs, out);
  g_string_append_printf (out, "%c", b->op);
  expression_stringify (b->rhs, out);
  g_string_append_printf (out, ")");
}

static void
binary_free (Expr *exp)
{
  Binary *b = (Binary *) exp;

  expression_free (b->lhs);
  expression_free (b->rhs);
  g_slice_free (Binary, b);
}

static ExprClass BinaryClass =
{
 .stringify = binary_stringify,
 .free = binary_free,
};

static Binary*
binary_new (int op)
{
  Binary *b;

  b = g_slice_new0 (Binary);
  b->expr.klass = &BinaryClass;
  b->op = op;

  return b;
}

/*
 * E -> T { B E }
 * T -> G | N T | C
 * G -> "(" E ")"
 * C -> F ":" V
 * B -> AND | OR
 * N -> NOT
 * F -> STRING
 * V -> STRING | NUMBER | BOOLEAN
*/

static inline GTokenType
parser_next (BoltQueryParser *parser)
{
  return g_scanner_get_next_token (parser->scanner);
}

static inline GTokenValue *
parser_value (BoltQueryParser *parser)
{
  return &parser->scanner->value;
}

/**
 * parser_expect:
 * @parser: The parser
 * @token: The expected Token
 *
 * Parser will advance and compare the next token to
 * the target @token. If it does not match will put
 * the parser into the error state and return %FALSE
 *
 * Returns: %TRUE if @token was found.
 **/
static gboolean
parser_expect (BoltQueryParser *parser, int token)
{
  GTokenType next;

  if (parser->error != NULL)
    return FALSE;

  next = g_scanner_get_next_token (parser->scanner);

  if ((int) next == token)
    return TRUE;

  g_set_error (&parser->error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
               "malformed input @ %u: unexpected token: %u",
               parser->scanner->position, token);

  return FALSE;
}

/**
 * parser_accept:
 * @parser: The parser
 * @token: Token to optionally consume
 *
 * Parser will peek the next token and if it matches @token
 * will consume it.
 *
 * Returns: %TRUE if @token was found and consumed.
 **/
static gboolean
parser_accept (BoltQueryParser *parser, int token)
{
  GTokenType next;

  if (parser->error != NULL)
    return FALSE;

  next = g_scanner_peek_next_token (parser->scanner);

  if ((int) next != token)
    return FALSE;

  next = g_scanner_get_next_token (parser->scanner);

  if ((int) next != token)
    g_warning ("internal parser error");

  return TRUE;
}

/**
 * parser_check:
 * @parser: The parser
 * @token: Token to check for
 *
 * Parser will peek the next token and if it matches @token
 * return %TRUE
 *
 * Returns: %FALSE if @token was not found.
 **/
static gboolean
parser_check (BoltQueryParser *parser, int token)
{
  GTokenType next;

  if (parser->error != NULL)
    return FALSE;

  next = g_scanner_peek_next_token (parser->scanner);

  return (int) next == token;
}

/**
 * parser_skip:
 * @parser: The parser.
 * @token: Target token to skip.
 *
 * Skip any number of @token.
 *
 * Returns: %TRUE if any token were skipped.
 **/
static gboolean
parser_skip (BoltQueryParser *parser, int token)
{
  gboolean ok;
  int count = 0;

  while (parser_check (parser, token))
    {
      ok = parser_expect (parser, token);

      if (!ok)
        return FALSE;

      count++;
    }

  return count > 0;
}

/* production rules */
static gboolean
parse_value (BoltQueryParser *parser, GValue *val)
{
  GTokenType  token;
  GTokenValue *value;

  token = parser_next (parser);
  value = parser_value (parser);

  switch (token)
    {
    case G_TOKEN_INT:
      g_value_init (val, G_TYPE_INT64);
      g_value_set_int64 (val, value->v_int64);
      return TRUE;

    case G_TOKEN_STRING:
      g_value_init (val, G_TYPE_STRING);
      g_value_set_string (val, value->v_string);
      return TRUE;

    case G_TOKEN_IDENTIFIER:
      g_value_init (val, G_TYPE_STRING);
      g_value_set_string (val, value->v_identifier);
      return TRUE;

    default:
      g_set_error (&parser->error,
                   G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   "malformed input @ %u: unexpected token: %u",
                   parser->scanner->position, token);
    }

  return FALSE;
}

static Expr * parse_term (BoltQueryParser *parser);
static Expr * parse_expression (BoltQueryParser *parser);

static Expr *
parse_condition (BoltQueryParser *parser)
{
  g_autoptr(Condition) c = condition_new ();
  gboolean ok;

  g_debug ("condition");

  ok = parser_expect (parser, G_TOKEN_IDENTIFIER);
  if (!ok)
    return NULL;

  c->field = g_strdup (parser_value (parser)->v_identifier);

  ok = parser_expect (parser, ':');

  if (!ok)
    return NULL;

  ok = parse_value (parser, &c->val);

  if (!ok)
    return NULL;

  return (Expr *) g_steal_pointer (&c);
}

static Expr *
parse_not (BoltQueryParser *parser)
{
  Unary *op;
  Expr *exp = NULL;
  gboolean ok;

  ok = parser_expect (parser, '-');
  if (!ok)
    return NULL;

  g_debug ("not");

  exp = parse_term (parser);
  if (!exp)
    return NULL;

  op = unary_new ('-');
  op->rhs = g_steal_pointer (&exp);

  return (Expr *) op;
}

static Expr *
parse_group (BoltQueryParser *parser)
{
  g_autoptr(Expr) exp = NULL;
  gboolean ok;

  ok = parser_expect (parser, '(');
  if (!ok)
    return FALSE;

  g_debug ("group");
  exp = parse_expression (parser);
  g_debug ("group exp done: %d", (int) ok);

  if (!exp)
    return NULL;

  ok = parser_expect (parser, ')');
  if (!ok)
    return FALSE;

  g_debug ("group done");
  return g_steal_pointer (&exp);
}

static Expr *
parse_term (BoltQueryParser *parser)
{
  g_debug ("term");

  if (parser_check (parser, '('))
    return parse_group (parser);
  else if (parser_check (parser, '-'))
    return parse_not (parser);
  else
    return parse_condition (parser);

  return NULL;
}

static Expr *
parse_or (BoltQueryParser *parser)
{
  gboolean ok;

  ok = parser_expect (parser, '|');
  if (!ok)
    return NULL;

  g_debug ("operator OR");

  return (Expr *) binary_new ('|');;
}

static Expr *
parse_and (BoltQueryParser *parser)
{
  parser_accept (parser, '&');

  g_debug ("operator AND");

  return (Expr *) binary_new ('&');
}

static Expr *
parse_expression (BoltQueryParser *parser)
{
  g_autoptr(Expr) op = NULL;
  g_autoptr(Expr) lhs = NULL;
  g_autoptr(Expr) rhs = NULL;
  Binary *b;
  gboolean ok;

  g_debug ("expression term");

  lhs = parse_term (parser);
  if (!lhs)
    return NULL;

  ok = parser_skip (parser, ' ');

  if (parser_check (parser, '|'))
    op = parse_or (parser);
  else if (parser_check (parser, '&') || ok)
    op = parse_and (parser);
  else
    return g_steal_pointer (&lhs);

  if (!op)
    return NULL;

  parser_skip (parser, ' ');

  rhs = parse_expression (parser);
  if (rhs == NULL)
    return NULL;

  b = (Binary *) op;
  b->lhs = g_steal_pointer (&lhs);
  b->rhs = g_steal_pointer (&rhs);

  return (Expr *) g_steal_pointer (&op);
}

static Expr *
bolt_query_parser_parse_string (BoltQueryParser *parser,
                                const char      *data,
                                gssize           length,
                                GError         **error)
{
  g_autoptr(Expr) exp = NULL;
  gboolean ok;

  if (length < 0)
    length = (gssize) strlen (data);

  if (length > G_MAXINT16)
    {
      g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                   "input too big: %" G_GSSIZE_FORMAT, length);
      return NULL;
    }

  g_scanner_input_text (parser->scanner, data, (guint) length);
  g_scanner_set_scope (parser->scanner, QL_SCOPE_DEFAULT);

  exp = parse_expression (parser);
  if (!exp)
    return exp;

  ok = parser_expect (parser, G_TOKEN_EOF);
  if (!ok)
    {
      *error = g_error_copy (parser->error);
      return NULL;
    }

  return g_steal_pointer (&exp);
}

int
main (int argc, char **argv)
{
  g_autoptr(BoltQueryParser) parser = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GString) str = NULL;
  g_autoptr(Expr) exp = NULL;

  if (argc != 2)
    return -1;

  parser = g_object_new (BOLT_TYPE_QUERY_PARSER, NULL);

  exp = bolt_query_parser_parse_string (parser, argv[1], -1, &error);

  if (!exp)
    {
     g_printerr ("ERROR: %s\n", error->message);
     return -1;
    }

  str = g_string_new ("");
  expression_stringify (exp, str);
  g_fprintf (stdout, "%s\n", str->str);

  return 0;
}
