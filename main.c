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
 * T -> C | "(" E ")" | N T
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

static int
parser_expect (BoltQueryParser *parser, guint n, ...)
{
  GTokenType *types;
  GTokenType token;
  va_list args;

  g_assert (n < 16);

  types = g_alloca (sizeof (GTokenType) * n);

  token = parser_peek (parser);

  va_start (args, n);
  for (guint i = 0; i < n; i++)
    types[i] = va_arg (args, GTokenType);
  va_end (args);

#if 0
  g_print (" expecting: ");
  for (guint i = 0; i < n; i++)
    g_print ("%u", types[i]);
  g_print ("\n");
#endif

  for (guint i = 0; i < n; i++)
    if (types[i] == token)
      return i;

  g_set_error (&parser->error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
               "malformed input @ %u: unexpected token: %u",
               parser->scanner->position, token);

  return -1;
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
  int r;

  g_debug ("condition");

  r = parser_expect (parser, 1, G_TOKEN_IDENTIFIER);
  if (r == -1)
    return FALSE;

  parser_next (parser);

  prop = g_strdup (parser->scanner->value.v_identifier);

  r = parser_expect (parser, 1, ':');

  if (r == -1)
    return FALSE;

  parser_next (parser);

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
parse_term (BoltQueryParser *parser)
{
  gboolean ok;
  int r;

  g_debug ("term");

  r = parser_expect (parser, 3, G_TOKEN_IDENTIFIER, '(', '-');
  if (r == -1)
    return FALSE;
  else if (r == 0)
    return parse_condition (parser);

  parser_next (parser);
  if (r == 1)
    {
      g_debug ("group");
      ok = parse_expression (parser);
      g_debug ("group exp done: %d", (int) ok);

      if (!ok)
        return FALSE;

      r = parser_expect (parser, 1, ')');
      if (r == -1)
        return FALSE;

      parser_next (parser);
      g_debug ("group done");
      return TRUE;
    }
  else if (r == 2)
    {
      return parse_not (parser);
    }

  return TRUE;
}

static gboolean
parse_expression (BoltQueryParser *parser)
{
  GTokenType token;
  gboolean ok;

  g_debug ("expression term");
  ok = parse_term (parser);
  if (!ok)
    return FALSE;

  token = parser_peek (parser);

  if (token != ' ')
    return TRUE;

  g_debug ("expression op");

  parser_next (parser);
  g_debug ("expression exp");

  ok = parse_expression (parser);

  return ok;
}

int
main(int argc, char **argv)
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
    ok = parser_expect (&parser, 1, G_TOKEN_EOF) != -1;

  if (!ok)
    g_printerr ("ERROR: %s\n", parser.error->message);

  return 0;
}
