#define _GNU_SOURCE 1

#include <glib.h>
#include <glib-object.h>

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

  next = parser_next (parser);

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


static gboolean
parse_value (BoltQueryParser *parser, GValue *value)
{
  GTokenType token;

  token = parser_next (parser);

  switch (token)
    {
    case G_TOKEN_INT:
      g_value_init (value, G_TYPE_INT64);
      g_value_set_int64 (value, parser->scanner->value.v_int64);
      return TRUE;

    case G_TOKEN_STRING:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, parser->scanner->value.v_string);
      return TRUE;

    case G_TOKEN_IDENTIFIER:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, parser->scanner->value.v_identifier);
      return TRUE;

    default:
      g_set_error (&parser->error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   "malformed input @ %u: unexpected token: %u",
                   parser->scanner->position, token);
    }

  return FALSE;
}

static gboolean
parse_condition (BoltQueryParser *parser)
{
  GValue val = G_VALUE_INIT;
  gboolean ok;
  char *prop;

  g_debug ("condition");

  ok = parser_expect (parser, G_TOKEN_IDENTIFIER);
  if (!ok)
    return FALSE;

  prop = g_strdup (parser->scanner->value.v_identifier);

  ok = parser_expect (parser, ':');

  if (!ok)
    return FALSE;

  ok = parse_value (parser, &val);

  if (!ok)
    return FALSE;

  g_print ("Parsed condition: %s:%s\n", prop, g_value_get_string (&val));

  return TRUE;
}

static gboolean parse_term (BoltQueryParser *parser);

static gboolean
parse_not (BoltQueryParser *parser)
{
  g_debug ("not");
  return parse_term (parser);
}

static gboolean parse_expression (BoltQueryParser *parser);

static gboolean
parse_group (BoltQueryParser *parser)
{
  gboolean ok;

  g_debug ("group");
  ok = parse_expression (parser);
  g_debug ("group exp done: %d", (int) ok);

  if (!ok)
    return FALSE;

  ok = parser_expect (parser, ')');
  if (!ok)
    return FALSE;

  g_debug ("group done");
  return TRUE;
}

static gboolean
parse_term (BoltQueryParser *parser)
{
  g_debug ("term");

  if (parser_accept (parser, '('))
    return parse_group (parser);
  else if (parser_accept (parser, '-'))
    return parse_not (parser);
  else
    return parse_condition (parser);

  return TRUE;
}

static gboolean
parse_expression (BoltQueryParser *parser)
{
  gboolean ok;

  g_debug ("expression term");

  ok = parse_term (parser);
  if (!ok)
    return FALSE;

  if (!parser_accept (parser, ' '))
    return TRUE;

  g_debug ("expression op");

  return parse_expression (parser);
}

int
main (int argc, char **argv)
{
  BoltQueryParser parser = {0, };
  GScanner *scanner;
  gboolean ok;
  BtId *id;

  if (argc != 2)
    return -1;

  scanner = g_scanner_new (&parser_config);

  id = g_object_new (BT_TYPE_ID, NULL);

  g_scanner_input_text (scanner, argv[1], strlen (argv[1]));
  g_scanner_set_scope (scanner, QL_SCOPE_DEFAULT);

  parser.scanner = scanner;
  ok = parse_expression (&parser);

  if (ok)
    ok = parser_expect (&parser, G_TOKEN_EOF);

  if (!ok)
    g_printerr ("ERROR: %s\n", parser.error->message);

  return 0;
}
