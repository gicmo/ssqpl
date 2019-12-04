#define _GNU_SOURCE 1

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>
#include <gio/gio.h>

#include <string.h>

#include "test-enums.h"

/*  */

typedef gint (*BoltParamValueCmp) (GParamSpec   *pspec,
                                   const GValue *a,
                                   const GValue *b);

/*  */
gboolean
bolt_enum_class_from_string (GEnumClass *klass,
                             const char *string,
                             gint       *enum_out,
                             GError    **error)
{
  const char *name;
  GEnumValue *ev;

  g_return_val_if_fail (G_IS_ENUM_CLASS (klass), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (string == NULL)
    {
      name = g_type_name_from_class ((GTypeClass *) klass);
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "empty string passed for enum class for '%s'",
                   name);
      return FALSE;
    }

  ev = g_enum_get_value_by_nick (klass, string);

  if (ev == NULL)
    {
      name = g_type_name_from_class ((GTypeClass *) klass);
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "invalid string '%s' for enum '%s'", string, name);
      return FALSE;
    }

  if (enum_out)
    *enum_out = ev->value;

  return TRUE;
}

gboolean
bolt_flags_class_from_string (GFlagsClass *flags_class,
                              const char  *string,
                              guint       *flags_out,
                              GError     **error)
{
  g_auto(GStrv) vals = NULL;
  const char *name;
  guint flags = 0;

  g_return_val_if_fail (G_IS_FLAGS_CLASS (flags_class), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (string == NULL)
    {
      name = g_type_name_from_class ((GTypeClass *) flags_class);
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "empty string passed for flags class for '%s'",
                   name);
      return FALSE;
    }

  vals = g_strsplit (string, "|", -1);

  for (guint i = 0; vals[i]; i++)
    {
      GFlagsValue *fv;
      char *nick;

      nick = g_strstrip (vals[i]);
      fv = g_flags_get_value_by_nick (flags_class, nick);

      if (fv == NULL)
        {
          name = g_type_name_from_class ((GTypeClass *) flags_class);
          g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                       "invalid flag '%s' for flags '%s'", string, name);

          return FALSE;
        }

      flags |= fv->value;
    }

  if (flags_out != NULL)
    *flags_out = flags;

  return TRUE;
}

#define bolt_flag_isset(flags_, flag_)  (!!(flags_ & flag_))
#define bolt_flag_isclear(flags_, flag_) (!(flags_ & flag_))

gboolean
bolt_param_is_int (GParamSpec *spec)
{
  if (G_IS_PARAM_SPEC_CHAR (spec) || G_IS_PARAM_SPEC_UCHAR (spec) ||
      G_IS_PARAM_SPEC_INT (spec) || G_IS_PARAM_SPEC_UINT (spec) ||
      G_IS_PARAM_SPEC_INT64 (spec) || G_IS_PARAM_SPEC_UINT64 (spec))
    return TRUE;

  return FALSE;
}

/*  */

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

  GHashTable *props;
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
  parser->props = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         (GDestroyNotify) g_free,
                                         (GDestroyNotify) g_param_spec_unref);
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

/* */
static gint
flags_cmp (GParamSpec   *pspec G_GNUC_UNUSED,
           const GValue *have,
           const GValue *want)
{
  guint flags = g_value_get_flags (have);
  guint f = g_value_get_flags (want);

 if (bolt_flag_isset (flags, f))
    return 0;
  else
    return -1;
}

/* */

/* AST */

typedef struct Expr Expr;
typedef struct ExprClass ExprClass;

typedef struct Condition Condition;
typedef struct Unary Unary;
typedef struct Binary Binary;

typedef void     (*ExprStringify) (Expr *expr, GString *out);
typedef gboolean (*ExprEval)      (Expr *expr, GObject *obj);
typedef void     (*ExprFree)      (Expr *expr);

struct ExprClass {
  ExprStringify stringify;
  ExprEval      eval;
  ExprFree      free;
};

struct Expr {
  ExprClass *klass;
};

struct Condition {
  Expr expr;

  /* */
  GParamSpec *field;
  GValue      val;

  /* optional */
  BoltParamValueCmp cmp;
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

static inline gboolean
expression_eval (Expr *exp, GObject *obj)
{
  return exp->klass->eval (exp, obj);
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
  g_string_append_printf (out, "%s:%s", c->field->name, tmp);
}

static gboolean
condition_eval (Expr *exp, GObject *obj)
{
  struct Condition *c = (struct Condition *) exp;
  g_auto(GValue) val = G_VALUE_INIT;
  int r;

  g_object_get_property (obj, c->field->name, &val);

  if (c->cmp)
    r = c->cmp (c->field, &c->val, &val);
  else
    r = g_param_values_cmp (c->field, &c->val, &val);

  return r == 0;
}

static void
condition_free (Expr *exp)
{
  struct Condition *c = (struct Condition *) exp;

  if (c->field)
    g_param_spec_unref (c->field);

  g_value_unset (&c->val);

  g_slice_free (Condition, c);
}

static ExprClass ConditionClass =
{
 .stringify = condition_stringify,
 .eval = condition_eval,
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

static gboolean
unary_eval (Expr *exp, GObject *obj)
{
  Unary *u = (Unary *) exp;

  return !expression_eval (u->rhs, obj);
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
 .eval = unary_eval,
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

static gboolean
binary_eval (Expr *exp, GObject *obj)
{
  Binary *b = (Binary *) exp;
  gboolean lhs;

  lhs = expression_eval (b->lhs, obj);

  if (b->op == '&' && lhs == FALSE)
    return FALSE; /* short-circuit: FALSE && rhs */
  else if (b->op == '|' && lhs == TRUE)
    return TRUE; /* short-circuit: TRUE || rhs */

  /* either 'TRUE && rhs' or 'FALSE || rhs'
   * -> result is just eval(rhs) */

  return expression_eval (b->rhs, obj);
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
 .eval = binary_eval,
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

static inline GTokenType
parser_token (BoltQueryParser *parser)
{
  return parser->scanner->token;
}

static const char *
parser_next_string (BoltQueryParser *parser)
{
  GTokenType  token;
  GTokenValue *value;
  const char  *str;

  token = parser_next (parser);

  if (token != G_TOKEN_STRING && token != G_TOKEN_IDENTIFIER)
    return NULL;

  value = parser_value (parser);

  if (token == G_TOKEN_STRING)
    str = value->v_string;
  else
    str = value->v_identifier;

  return str;
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
parse_value_int (BoltQueryParser *parser,
                 Condition       *cond)
{
  GValue *dest = &cond->val;
  GType type = G_VALUE_TYPE (dest);
  GTokenType  token;
  GTokenValue *value;
  gint64 v;

  token = parser_next (parser);

  if (token != G_TOKEN_INT)
    {
      g_set_error (&parser->error,
                   G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   "malformed input @ %u: unexpected token: %u",
                   parser->scanner->position, token);
      return FALSE;
    }

  value = parser_value (parser);
  v = value->v_int64;
  /* TODO: validate */

  switch (type)
    {
    case G_TYPE_CHAR:
      g_value_set_schar (dest, (gint8) v);
      break;

    case G_TYPE_UCHAR:
      g_value_set_uchar (dest, (guchar) v);
      break;

    case G_TYPE_INT:
      g_value_set_int (dest, (int) v);
      break;

    case G_TYPE_UINT:
      g_value_set_uint (dest, (unsigned int) v);
      break;

    case G_TYPE_INT64:
      g_value_set_int64 (dest, v);
      break;

    case G_TYPE_UINT64:
      g_value_set_uint64 (dest, (guint64) v);
      break;

    default:
      g_set_error (&parser->error,
                   G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   "invalid value type '%s' for field",
                   g_type_name (type));
      return FALSE;
    }

  return TRUE;
}

static gboolean
parse_value_str (BoltQueryParser *parser,
                 Condition       *cond)
{
  const char *str;

  str = parser_next_string (parser);

  if (str == NULL)
    {
      g_set_error (&parser->error,
                   G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   "malformed input @ %u: unexpected token: %u",
                   parser->scanner->position, parser_token (parser));
      return FALSE;
    }

  g_value_set_string (&cond->val, str);

  return TRUE;
}

static gboolean
parse_value_enum (BoltQueryParser *parser,
                  Condition       *cond)
{
  GParamSpecEnum *enum_spec;
  const char *str;
  gboolean ok;
  gint val;

  enum_spec = G_PARAM_SPEC_ENUM (cond->field);
  str = parser_next_string (parser);

  if (str == NULL)
    {
      g_set_error (&parser->error,
                   G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   "malformed input @ %u: unexpected token: %u",
                   parser->scanner->position, parser_token (parser));
      return FALSE;
    }

  ok = bolt_enum_class_from_string (enum_spec->enum_class,
                                    str,
                                    &val,
                                    &parser->error);

  if (ok)
    g_value_set_enum (&cond->val, val);

  return ok;
}

static gboolean
parse_value_flags (BoltQueryParser *parser,
                   Condition       *cond)
{
  GParamSpecFlags *flags_spec;
  const char *str;
  gboolean ok;
  guint val;

  flags_spec = G_PARAM_SPEC_FLAGS (cond->field);
  str = parser_next_string (parser);

  if (str == NULL)
    {
      g_set_error (&parser->error,
                   G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   "malformed input @ %u: unexpected token: %u",
                   parser->scanner->position, parser_token (parser));
      return FALSE;
    }

  ok = bolt_flags_class_from_string (flags_spec->flags_class,
                                    str,
                                    &val,
                                    &parser->error);

  if (ok)
    {
      g_value_set_flags (&cond->val, val);
      cond->cmp = flags_cmp;
    }

  return ok;
}

static gboolean
unsupported_value (BoltQueryParser *parser,
                   Condition       *cond)
{
  g_set_error (&parser->error,
               G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
               "malformed input @ %u: unsupported value: %s",
               parser->scanner->position,
               g_type_name (cond->field->value_type));
  return FALSE;
}

static Expr * parse_term (BoltQueryParser *parser);
static Expr * parse_expression (BoltQueryParser *parser);

static Expr *
parse_condition (BoltQueryParser *parser)
{
  g_autoptr(Condition) c = condition_new ();
  const char *name;
  GParamSpec *spec;
  gboolean ok;

  g_debug ("condition");

  ok = parser_expect (parser, G_TOKEN_IDENTIFIER);
  if (!ok)
    return NULL;

  name = parser_value (parser)->v_identifier;

  if (name == NULL)
    {
      g_set_error (&parser->error,
                   G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   "empty identifier");
      return NULL;
    }

  spec = g_hash_table_lookup (parser->props, name);
  if (spec == NULL)
    {
      g_set_error (&parser->error,
                   G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   "unknown field: '%s'", name);
      return NULL;
    }

  c->field = g_param_spec_ref (spec);
  g_value_init (&c->val, c->field->value_type);

  ok = parser_expect (parser, ':');

  if (!ok)
    return NULL;

  if (G_IS_PARAM_SPEC_STRING (spec))
    ok = parse_value_str (parser, c);
  else if (bolt_param_is_int (spec))
    ok = parse_value_int (parser, c);
  else if (G_IS_PARAM_SPEC_ENUM (spec))
    ok = parse_value_enum (parser, c);
  else if (G_IS_PARAM_SPEC_FLAGS (spec))
    ok = parse_value_flags (parser, c);
  else
    ok = unsupported_value (parser, c);
  /*  */

  if (!ok)
    return NULL;

  if (!g_value_type_compatible (c->field->value_type,
                                G_VALUE_TYPE (&c->val)))
    {
      g_set_error (&parser->error,
                   G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   "value '%s' incompatible with field: '%s' (%s)",
                   g_type_name (G_VALUE_TYPE (&c->val)),
                   c->field->name,
                   g_type_name (c->field->value_type));
      return NULL;
    }

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

  g_assert_not_reached ();
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
    {
      *error = g_error_copy (parser->error);
      return exp;
    }

  ok = parser_expect (parser, G_TOKEN_EOF);
  if (!ok)
    {
      *error = g_error_copy (parser->error);
      return NULL;
    }

  return g_steal_pointer (&exp);
}

static void
bolt_query_parser_register_object_props (BoltQueryParser *parser,
                                         GObjectClass    *klass)
{
  g_autofree GParamSpec **props = NULL;
  guint n;

  props = g_object_class_list_properties (klass, &n);

  for (guint i = 0; i < n; i++)
    {
      GParamSpec *spec = props[i];
      g_hash_table_insert (parser->props,
                           g_strdup (spec->name),
                           g_param_spec_ref (spec));
      g_debug ("registered '%s'", spec->name);
    }
}

/* *** Tiny test object with  */
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
  PROP_INT,
  PROP_ENUM,
  PROP_FLAG,
  PROP_ID_LAST
};


static GParamSpec *id_props[PROP_ID_LAST] = {NULL, };


static void
bt_id_init (BtId *be G_GNUC_UNUSED)
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

    case PROP_INT:
      g_value_set_int (value, 42);
      break;

    case PROP_ENUM:
      g_value_set_enum (value, BOLT_TEST_TWO);
      break;

    case PROP_FLAG:
      g_value_set_flags (value, BOLT_KITT_DEFAULT);
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

  id_props[PROP_INT] =
    g_param_spec_int ("int", "Int",
                      NULL,
                      -42, 256, 0,
                      G_PARAM_READABLE |
                      G_PARAM_STATIC_NICK |
                      G_PARAM_STATIC_BLURB);

  id_props[PROP_ENUM] =
    g_param_spec_enum ("enum", "Enum", NULL,
                       BOLT_TYPE_TEST_ENUM,
                       BOLT_TEST_UNKNOWN,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_NICK |
                       G_PARAM_STATIC_BLURB);

  id_props[PROP_FLAG] =
    g_param_spec_flags ("flags", "Flags", NULL,
                        BOLT_TYPE_KITT_FLAGS,
                        BOLT_KITT_DISABLED,
                        G_PARAM_READABLE |
                        G_PARAM_STATIC_NICK |
                        G_PARAM_STATIC_BLURB);

  g_object_class_install_properties (gobject_class,
                                     PROP_ID_LAST,
                                     id_props);


}

/* */

int
main (int argc, char **argv)
{
  g_autoptr(BoltQueryParser) parser = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GString) str = NULL;
  g_autoptr(Expr) exp = NULL;
  g_autoptr(BtId) id = NULL;
  gboolean res;

  if (argc != 2)
    return -1;

  id = g_object_new (BT_TYPE_ID, NULL);

  parser = g_object_new (BOLT_TYPE_QUERY_PARSER, NULL);

  bolt_query_parser_register_object_props (parser, G_OBJECT_GET_CLASS (id));

  exp = bolt_query_parser_parse_string (parser, argv[1], -1, &error);

  if (!exp)
    {
     g_printerr ("ERROR: %s\n", error->message);
     return -1;
    }

  str = g_string_new ("");
  expression_stringify (exp, str);
  g_fprintf (stdout, "%s", str->str);
  res = expression_eval (exp, G_OBJECT (id));
  g_fprintf (stdout, " = %s\n", (res ? "true" : "false"));

  return 0;
}
