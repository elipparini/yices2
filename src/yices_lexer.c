/*
 * Lexer for Yices language
 * - separators are ( ) : spaces, EOF, ; and "
 *
 * - strings are delimited by " with escaped char \n, \t, etc. allowed
 * 
 * - two kinds of numeric literals are recognized
 *     TK_NUM_RATIONAL:  <optional_sign><digits>/<digits>
 *                    or <optional_sign><digits>
 *     TK_NUM_FLOAT: 
 *       <optional_sign> <digits> . <digits>
 *       <optional_sign> <digits> <exp> <optional_sign> <digits>
 *       <optional_sign> <digits> . <digits> <exp> <optional_sign> <digits>
 *
 *   (the two formats recognized by string-to-rational conversions in rational.c)
 *
 * - bit-vector literals are written 0b<binary digits> (cf. bv_constants.c)
 *   (added 5/10/07) can also be written 0x<hexa digits> 
 * 
 * - comments start with ; and extend to the end of the line
 */

#include <ctype.h>
#include <assert.h>

/*
 * yices_hash_keywords.h is generated by gperf
 * from input file yices_keywords.txt
 */
#include "yices_lexer.h"
#include "yices_hash_keywords.h"


/*
 * All keywords
 */
static keyword_t yices_keywords[] = {
  // type keywords
  { "bool", TK_BOOL },
  { "int", TK_INT },
  { "real", TK_REAL },
  { "bitvector", TK_BITVECTOR },
  { "scalar", TK_SCALAR },
  { "tuple", TK_TUPLE },
  { "->", TK_ARROW },

  // term keywords
  { "true", TK_TRUE },
  { "false", TK_FALSE },
  { "if", TK_IF },
  { "ite", TK_ITE },
  { "=", TK_EQ },
  { "/=", TK_DISEQ },
  { "distinct", TK_DISTINCT },
 
  { "or", TK_OR },
  { "and", TK_AND },
  { "not", TK_NOT },
  { "xor", TK_XOR },
  { "<=>", TK_IFF }, 
  { "=>", TK_IMPLIES },
  { "mk-tuple", TK_MK_TUPLE },
  { "select", TK_SELECT },
  { "tuple-update", TK_UPDATE_TUPLE },
  { "update", TK_UPDATE },
  { "forall", TK_FORALL },
  { "exists", TK_EXISTS },
  { "lambda", TK_LAMBDA },

  // arithmetic keywords
  { "+", TK_ADD },
  { "-", TK_SUB },
  { "*", TK_MUL },
  { "/", TK_DIV },
  { "^", TK_POW },
  { "<", TK_LT }, 
  { "<=", TK_LE },
  { ">", TK_GT },
  { ">=", TK_GE },

  // bitvector keywords
  { "mk-bv", TK_MK_BV },
  { "bv-add", TK_BV_ADD },
  { "bv-sub", TK_BV_SUB },
  { "bv-mul", TK_BV_MUL },
  { "bv-neg", TK_BV_NEG },
  { "bv-not", TK_BV_NOT },
  { "bv-and", TK_BV_AND },
  { "bv-or", TK_BV_OR },
  { "bv-xor", TK_BV_XOR },
  { "bv-nand", TK_BV_NAND },
  { "bv-nor", TK_BV_NOR },
  { "bv-xnor", TK_BV_XNOR },
  { "bv-shift-left0", TK_BV_SHIFT_LEFT0 },
  { "bv-shift-left1", TK_BV_SHIFT_LEFT1 },
  { "bv-shift-right0", TK_BV_SHIFT_RIGHT0 },
  { "bv-shift-right1", TK_BV_SHIFT_RIGHT1 },
  { "bv-ashift-right", TK_BV_ASHIFT_RIGHT },
  { "bv-rotate-left", TK_BV_ROTATE_LEFT },
  { "bv-rotate-right", TK_BV_ROTATE_RIGHT },
  { "bv-extract", TK_BV_EXTRACT },
  { "bv-concat", TK_BV_CONCAT },
  { "bv-repeat", TK_BV_REPEAT },
  { "bv-sign-extend", TK_BV_SIGN_EXTEND },
  { "bv-zero-extend", TK_BV_ZERO_EXTEND },
  { "bv-ge", TK_BV_GE },
  { "bv-gt", TK_BV_GT },
  { "bv-le", TK_BV_LE },
  { "bv-lt", TK_BV_LT },
  { "bv-sge", TK_BV_SGE },
  { "bv-sgt", TK_BV_SGT },
  { "bv-sle", TK_BV_SLE },
  { "bv-slt", TK_BV_SLT },

  // more bitvector keywords
  { "bv-shl", TK_BV_SHL },
  { "bv-lshr", TK_BV_LSHR },
  { "bv-ashr", TK_BV_ASHR },
  { "bv-div", TK_BV_DIV },
  { "bv-rem", TK_BV_REM },
  { "bv-sdiv", TK_BV_SDIV },
  { "bv-srem", TK_BV_SREM },
  { "bv-smod", TK_BV_SMOD },
  { "bv-redor", TK_BV_REDOR },
  { "bv-redand", TK_BV_REDAND },
  { "bv-comp", TK_BV_COMP },

  // other keywords
  { "let", TK_LET },
  { "define-type", TK_DEFINE_TYPE },
  { "define", TK_DEFINE },
  { "assert", TK_ASSERT },
  { "check", TK_CHECK },
  { "push", TK_PUSH },
  { "pop", TK_POP },
  { "reset", TK_RESET },
  { "dump-context", TK_DUMP_CONTEXT },
  { "exit", TK_EXIT },
  { "echo", TK_ECHO },
  { "include", TK_INCLUDE },
  { "show-model", TK_SHOW_MODEL },
  { "eval", TK_EVAL },
  { "set-param", TK_SET_PARAM },
  { "show-param", TK_SHOW_PARAM },
  { "show-params", TK_SHOW_PARAMS },
  { "show-stats", TK_SHOW_STATS },
  { "reset-stats", TK_RESET_STATS },
  { "set-timeout", TK_SET_TIMEOUT },

  // end-marker
  { NULL, 0 },
};

/*
 * name for each token
 */
static char *token_string[NUM_YICES_TOKENS];


/*
 * Initialize token2string table
 */
static void init_token2string() {
  keyword_t *kw;

  // keywords
  kw = yices_keywords;
  while (kw->word != NULL) {
    token_string[kw->tk] = kw->word;
    kw ++;
  }

  // other tokens
  token_string[TK_LP] = "(";
  token_string[TK_RP] = ")";
  token_string[TK_COLON_COLON] = "::";
  token_string[TK_EOS] = "<end-of-stream>";
  token_string[TK_STRING] = "<string>";
  token_string[TK_NUM_RATIONAL] = "<rational>";
  token_string[TK_NUM_FLOAT] = "<float>";
  token_string[TK_BV_CONSTANT] = "<bv-constant>";
  token_string[TK_HEX_CONSTANT] = "<hex-constant>";
  token_string[TK_SYMBOL] = "<symbol>";

  token_string[TK_OPEN_STRING] = "<bad-string>";
  token_string[TK_EMPTY_BVCONST] = "<bad-bvconst>";
  token_string[TK_EMPTY_HEXCONST] = "<bad-hexconst>";
  token_string[TK_INVALID_NUM] = "<bad-float>";
  token_string[TK_ZERO_DIVISOR] = "<zero-divisor-in-rational>";
  token_string[TK_ERROR] = "<error>";
}


/*
 * Lexer initialization
 */
int32_t init_yices_file_lexer(lexer_t *lex, char *filename) {
  init_token2string();
  return init_file_lexer(lex, filename);
}

void init_yices_stream_lexer(lexer_t *lex, FILE *f, char *name) {
  init_token2string();
  init_stream_lexer(lex, f, name);
}

void init_yices_string_lexer(lexer_t *lex, char *data, char *name) {
  init_token2string();
  init_string_lexer(lex, data, name);
}



/*
 * Get token_string
 */
char *yices_token_to_string(yices_token_t tk) {
  assert(0 <= tk && tk < NUM_YICES_TOKENS);
  return token_string[tk];
}

/*
 * Check whether c is a separator character (cannot be part of a symbol)
 */
static bool is_yices_sep(int c) {
  return isspace(c) || c == EOF || c == '(' || c == ')' ||
    c == ':' || c == ';' || c == '"';
}


/*
 * Read a string literal: 
 * - lex->current_char == '"' and lex->buffer is empty
 */
static yices_token_t read_string(lexer_t *lex) {
  yices_token_t tk;
  int c, x;
  reader_t *rd;
  string_buffer_t *buffer;

  rd = &lex->reader;
  buffer = lex->buffer;
  assert(reader_current_char(rd) == '"');

  c = reader_next_char(rd);

  for (;;) {
    if (c == '"') { // end of string
      // consume the closing quote
      reader_next_char(rd);
      tk = TK_STRING; 
      break;
    }
    if (c == '\n' || c == EOF) { // missing quotes
      tk = TK_OPEN_STRING; 
      break;
    }
    if (c == '\\') {
      // escape sequence
      c = reader_next_char(rd);
      switch (c) {
      case 'n': c = '\n'; break;
      case 't': c = '\t'; break;
      default:
	if ('0' <= c && c <= '7') {
	  // read at most 2 more octal digits
	  x = c - '0';
	  c = reader_next_char(rd);
	  if ('0' <= c && c <= '7') {
	    x = 8 * x + (c - '0');
	    c = reader_next_char(rd);
	    if ('0' <= c && c <= '7') {
	      x = 8 * x + (c - '0');
	      c = reader_next_char(rd);
	    }
	  }
	  // x = character built from the octal digits
	  // c = character after octal digit
	  string_buffer_append_char(buffer, x);
	  continue;
	} // else skip '\': copy c in the buffer
	break;
      }
    }
    string_buffer_append_char(buffer, c);
    c = reader_next_char(rd);
  }

  string_buffer_close(buffer);
  return tk;
}

/*
 * Read a bitvector constant:
 * lex->current_char = 'b' and lex->buffer contains "0"
 */
static yices_token_t read_bv_constant(lexer_t *lex) {
  reader_t *rd;
  string_buffer_t *buffer;
  int c;  

  rd = &lex->reader;
  c = reader_current_char(rd);
  buffer = lex->buffer;

  do {
    string_buffer_append_char(buffer, c);
    c = reader_next_char(rd);
  } while (c == '0' || c == '1');
  string_buffer_close(buffer);

  if (string_buffer_length(buffer) <= 2) {
    return TK_EMPTY_BVCONST; // empty constant
  } else {
    return TK_BV_CONSTANT;    
  }
}

/*
 * Read a hexadecimal constant:
 * lex->current_char = 'x' and lex->buffer contains "0"
 */
static yices_token_t read_hex_constant(lexer_t *lex) {
  reader_t *rd;
  string_buffer_t *buffer;
  int c;  

  rd = &lex->reader;
  c = reader_current_char(rd);
  buffer = lex->buffer;

  do {
    string_buffer_append_char(buffer, c);
    c = reader_next_char(rd);
  } while (isxdigit(c));
  string_buffer_close(buffer);

  if (string_buffer_length(buffer) <= 2) {
    return TK_EMPTY_HEXCONST; // empty constant
  } else {
    return TK_HEX_CONSTANT;    
  }
}

/*
 * Read a symbol or keyword
 * lex->buffer contains one char (not a separator or digit)
 * char = next character after that.
 */
static yices_token_t read_symbol(lexer_t *lex) {
  reader_t *rd;
  string_buffer_t *buffer;
  int c;
  token_t tk;
  const keyword_t *kw;

  rd = &lex->reader;
  c = reader_current_char(rd);
  buffer = lex->buffer;

  while (! is_yices_sep(c)) {
    string_buffer_append_char(buffer, c);
    c = reader_next_char(rd);
  }

  string_buffer_close(buffer);

  tk = TK_SYMBOL;
  kw = in_yices_kw(buffer->data, buffer->index);
  if (kw != NULL) {
    tk = kw->tk;
  }

  return tk;
}

/*
 * Read a number
 * lex->buffer contains <optional_sign> and a single digit
 * current_char = what's after the digit in buffer.
 */
static yices_token_t read_number(lexer_t *lex) {
  reader_t *rd;
  string_buffer_t *buffer;
  int c, all_zeros;
  yices_token_t tk;

  rd = &lex->reader;
  c = reader_current_char(rd);
  buffer = lex->buffer;
  tk = TK_NUM_RATIONAL; // default 

  while (isdigit(c)) {
    string_buffer_append_char(buffer, c);
    c = reader_next_char(rd);
  }

  if (c == '/') {
    // denominator
    string_buffer_append_char(buffer, c);
    c = reader_next_char(rd);
    if (! isdigit(c)) {
      tk = TK_INVALID_NUM;
      goto done;
    }
    all_zeros = true;
    do {
      if (c != '0') all_zeros = false;
      string_buffer_append_char(buffer, c);
      c = reader_next_char(rd);
    } while (isdigit(c));

    if (all_zeros) tk = TK_ZERO_DIVISOR;
    // else tk = TK_NUM_RATIONAL
    goto done;
  }

  if (c == '.') {
    tk = TK_NUM_FLOAT;
    // fractional part
    string_buffer_append_char(buffer, c);
    c = reader_next_char(rd);
    if (! isdigit(c)) {
      tk = TK_INVALID_NUM;
      goto done;
    }
    do {
      string_buffer_append_char(buffer, c);
      c = reader_next_char(rd);
    } while (isdigit(c));
  }

  if (c == 'e' || c == 'E') {
    tk = TK_NUM_FLOAT;
    // exponent
    string_buffer_append_char(buffer, c);
    c = reader_next_char(rd);
    if (c == '+' || c == '-') {
      string_buffer_append_char(buffer, c);
      c = reader_next_char(rd);
    }
    if (! isdigit(c)) {
      tk = TK_INVALID_NUM;
      goto done;
    }
    do {
      string_buffer_append_char(buffer, c);
      c = reader_next_char(rd);
    } while (isdigit(c));
  }

 done:
  string_buffer_close(buffer);
  return tk;
}

/*
 * Read next token and return its type tk
 * - set lex->token to tk 
 * - set lex->tk_pos, etc.
 * - if token is TK_STRING, TK_NUM_RATIONAL, TK_NUM_FLOAT, TK_BV_CONSTANT, TK_SYMBOL, TK_ERROR,
 *   the token value is stored in lex->buffer (as a string).
 */
yices_token_t next_yices_token(lexer_t *lex) {
  yices_token_t tk;
  reader_t *rd;
  string_buffer_t *buffer;
  int c;  

  rd = &lex->reader;
  c = reader_current_char(rd);
  buffer = lex->buffer;
  string_buffer_reset(buffer);

  // skip spaces and comments
  for (;;) {
    while (isspace(c)) c = reader_next_char(rd);
    if (c != ';') break;
    do { // read to end-of-line or eof
      c = reader_next_char(rd);
    } while (c != '\n' && c != EOF);
  }
  
  // record token position (start of token)
  lex->tk_pos = rd->pos;
  lex->tk_line = rd->line;
  lex->tk_column = rd->column;
  
  switch (c) {
  case '(': 
    tk = TK_LP;
    goto next_then_return;
  case ')': 
    tk = TK_RP;
    goto next_then_return;
  case EOF:
    tk = TK_EOS;
    goto done;
  case ':':
    c = reader_next_char(rd);
    if (c == ':') {
      tk = TK_COLON_COLON; 
      goto next_then_return;
    } else {
      // store ':' in the buffer since that may be used for reporting errors
      string_buffer_append_char(buffer, ':');
      string_buffer_close(buffer);
      tk = TK_ERROR;
      goto done;
    }
  case '"':
    tk = read_string(lex);
    goto done;
  case '+':    
  case '-':
    string_buffer_append_char(buffer, c);
    c = reader_next_char(rd);
    if (isdigit(c)) {
      string_buffer_append_char(buffer, c);
      reader_next_char(rd);
      tk = read_number(lex);
    } else {
      tk = read_symbol(lex);
    }
    goto done;
    
  case '0':
    string_buffer_append_char(buffer, c);
    c = reader_next_char(rd);
    if (c == 'b') {
      tk = read_bv_constant(lex);
    } else if (c == 'x') {
      tk = read_hex_constant(lex);
    } else {
      tk = read_number(lex);
    }
    goto done;      

  case '1':
  case '2':
  case '3':
  case '4': 
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    string_buffer_append_char(buffer, c);
    reader_next_char(rd);
    tk = read_number(lex);
    goto done;
    
  default: // symbol or keyword
    string_buffer_append_char(buffer, c);
    reader_next_char(rd);
    tk = read_symbol(lex);
    goto done;
  }
  
  /*
   * read next character and exit
   */
 next_then_return:
  reader_next_char(rd);


 done:
  lex->token = tk;
  return tk;
}

