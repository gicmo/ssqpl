#define _GNU_SOURCE 1

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>

#include <string.h>


#define BT_TYPE_ID bt_id_get_type ()
G_DECLARE_FINAL_TYPE (BtId, bt_id, BT, ID, GObject)

struct _BtId
{
  GObject parent;
};

G_DEFINE_TYPE (BtId, bt_id, G_TYPE_OBJECT)

enum {
  PROP_ID_0,
  PROP_ID,
  PROP_ID_LAST
};


static GParamSpec *id_props[PROP_ID_LAST] = {NULL, };


static void
bt_id_init (BtId *be)
{
}

static void
bt_id_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_string (value, "bolt-id");
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bt_id_class_init (BtIdClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bt_id_get_property;

  id_props[PROP_ID] =
    g_param_spec_string ("id", "Id", NULL,
                         NULL,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK |
                         G_PARAM_STATIC_BLURB);

  g_object_class_install_properties (gobject_class,
                                     PROP_ID_LAST,
                                     id_props);


}


static GScannerConfig parser_config =
{
  ("\t\r\n"),		/* cset_skip_characters */
  (
  G_CSET_a_2_z
  G_CSET_A_2_Z
  ),			/* cset_identifier_first */
  (
  G_CSET_a_2_z
  G_CSET_A_2_Z
  G_CSET_DIGITS
  "-"
  ),			/* cset_identifier_nth */
  ("#\n"),		/* cpair_comment_single */
  TRUE,		/* case_sensitive */
  TRUE,		/* skip_comment_multi */
  TRUE,			/* skip_comment_single */
  FALSE,		/* scan_comment_multi */
  TRUE,			/* scan_identifier */
  FALSE,		/* scan_identifier_1char */
  FALSE,		/* scan_identifier_NULL */
  TRUE,			/* scan_symbols */
  FALSE,		/* scan_binary */
  FALSE,		/* scan_octal */
  TRUE,			/* scan_float */
  FALSE,		/* scan_hex */
  FALSE,		/* scan_hex_dollar */
  TRUE,		/* scan_string_sq */
  TRUE,		/* scan_string_dq */
  TRUE,		/* numbers_2_int */
  FALSE,			/* int_2_float */
  FALSE,		/* identifier_2_string */
  TRUE,		/* char_2_token */
  TRUE,			/* symbol_2_token */
  FALSE,		/* scope_0_fallback */
  TRUE,
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


/* AST */

typedef struct Expr Expr;
typedef struct ExprClass ExprClass;

typedef struct Condition Condition;
typedef struct Unary Unary;
typedef struct Binary Binary;

typedef void (*ExprDump) (Expr *expr, FILE *out);
typedef void (*ExprFree) (Expr *expr);

struct ExprClass {
  ExprDump dump;
  ExprFree free;
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
expression_dump (Expr *exp, FILE *out)
{
  exp->klass->dump (exp, out);
}

static inline void
expression_free (Expr *exp)
{
  exp->klass->free (exp);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Expr, expression_free)

/*  */
static void
condition_dump (Expr *exp, FILE *out)
{
  g_autofree char *tmp = NULL;
  struct Condition *c;

  c = (struct Condition *) exp;

  tmp = g_strdup_value_contents (&c->val);
  g_fprintf (out, "%s:%s", c->field, tmp);
}

static void
condition_free (Expr *exp)
{
  struct Condition *c = (struct Condition *) exp;

  g_free (c->field);
  g_value_unset (&c->val);

  g_slice_free (struct Condition, exp);
}

static ExprClass ConditionClass =
{
 .dump = condition_dump,
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
unary_dump (Expr *exp, FILE *out)
{
  struct Unary *u = (struct Unary *) exp;

  g_fprintf (out, "%c(", u->op);
  expression_dump (u->rhs, out);
  g_fprintf (out, ")");
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
 .dump = unary_dump,
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
binary_dump (Expr *exp, FILE *out)
{
  Binary *b = (Binary *) exp;

  g_fprintf (out, "(");
  expression_dump (b->lhs, out);
  g_fprintf (out, "%c", b->op);
  expression_dump (b->rhs, out);
  g_fprintf (out, ")");
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
 .dump = binary_dump,
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

typedef struct BoltQueryParser {

  GScanner *scanner;
  GError *error;

} BoltQueryParser;

static inline GTokenType
parser_peek (BoltQueryParser *parser)
{
  return g_scanner_peek_next_token (parser->scanner);
}

static inline GTokenType
parser_next (BoltQueryParser *parser)
{
  return g_scanner_get_next_token (parser->scanner);
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
  GTokenType next;
  int count = 0;

  while ((int) (next = parser_peek (parser)) == token)
    {
      next = parser_next (parser);

      if ((int) next != token)
        {
          g_warning ("internal parser error");
          break;
        }

      count++;
    }

  return count > 0;
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

/* production rules */
static gboolean
parse_value (BoltQueryParser *parser, GValue *val)
{
  GTokenType  token;
  GTokenValue *value;

  token = parser_next (parser);
  value = &parser->scanner->value;

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

  c->field = g_strdup (parser->scanner->value.v_identifier);

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
parse_input (BoltQueryParser *parser,
             const char      *data)
{
  g_autoptr(Expr) exp = NULL;
  gboolean ok;

  g_scanner_input_text (parser->scanner, data, strlen (data));
  g_scanner_set_scope (parser->scanner, QL_SCOPE_DEFAULT);

  exp = parse_expression (parser);
  if (!exp)
    return exp;

  ok = parser_expect (parser, G_TOKEN_EOF);
  if (!ok)
    return NULL;

  return g_steal_pointer (&exp);
}

int
main (int argc, char **argv)
{
  BoltQueryParser parser = {0, };
  g_autoptr(Expr) exp = NULL;
  GScanner *scanner;
  BtId *id;

  if (argc != 2)
    return -1;

  scanner = g_scanner_new (&parser_config);

  id = g_object_new (BT_TYPE_ID, NULL);

  parser.scanner = scanner;
  exp = parse_input (&parser, argv[1]);

  if (!exp)
    {
     g_printerr ("ERROR: %s\n", parser.error->message);
     return -1;
    }

  expression_dump (exp, stdout);
  g_fprintf (stdout, "\n");

  return 0;
}
