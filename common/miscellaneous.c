/* miscellaneous.c - Stuff not fitting elsewhere
 *	Copyright (C) 2003, 2006 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either
 *
 *   - the GNU Lesser General Public License as published by the Free
 *     Software Foundation; either version 3 of the License, or (at
 *     your option) any later version.
 *
 * or
 *
 *   - the GNU General Public License as published by the Free
 *     Software Foundation; either version 2 of the License, or (at
 *     your option) any later version.
 *
 * or both in parallel, as here.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <errno.h>

#define JNLIB_NEED_LOG_LOGV
#include "util.h"
#include "iobuf.h"
#include "i18n.h"

/* Used by libgcrypt for logging.  */
static void
my_gcry_logger (void *dummy, int level, const char *fmt, va_list arg_ptr)
{
  (void)dummy;

  /* Map the log levels.  */
  switch (level)
    {
    case GCRY_LOG_CONT: level = JNLIB_LOG_CONT; break;
    case GCRY_LOG_INFO: level = JNLIB_LOG_INFO; break;
    case GCRY_LOG_WARN: level = JNLIB_LOG_WARN; break;
    case GCRY_LOG_ERROR:level = JNLIB_LOG_ERROR; break;
    case GCRY_LOG_FATAL:level = JNLIB_LOG_FATAL; break;
    case GCRY_LOG_BUG:  level = JNLIB_LOG_BUG; break;
    case GCRY_LOG_DEBUG:level = JNLIB_LOG_DEBUG; break;
    default:            level = JNLIB_LOG_ERROR; break;
    }
  log_logv (level, fmt, arg_ptr);
}


/* This function is called by libgcrypt on a fatal error.  */
static void
my_gcry_fatalerror_handler (void *opaque, int rc, const char *text)
{
  (void)opaque;

  log_fatal ("libgcrypt problem: %s\n", text ? text : gpg_strerror (rc));
  abort ();
}


/* This function is called by libgcrypt if it ran out of core and
   there is no way to return that error to the caller.  We do our own
   function here to make use of our logging functions. */
static int
my_gcry_outofcore_handler (void *opaque, size_t req_n, unsigned int flags)
{
  static int been_here;  /* Used to protect against recursive calls. */

  (void)opaque;

  if (!been_here)
    {
      been_here = 1;
      if ( (flags & 1) )
        log_fatal (_("out of core in secure memory "
                     "while allocating %lu bytes"), (unsigned long)req_n);
      else
        log_fatal (_("out of core while allocating %lu bytes"),
                   (unsigned long)req_n);
    }
  return 0; /* Let libgcrypt call its own fatal error handler.
               Actually this will turn out to be
               my_gcry_fatalerror_handler. */
}


/* Setup libgcrypt to use our own logging functions.  Should be used
   early at startup. */
void
setup_libgcrypt_logging (void)
{
  gcry_set_log_handler (my_gcry_logger, NULL);
  gcry_set_fatalerror_handler (my_gcry_fatalerror_handler, NULL);
  gcry_set_outofcore_handler (my_gcry_outofcore_handler, NULL);
}

/* A wrapper around gcry_cipher_algo_name to return the string
   "AES-128" instead of "AES".  Given that we have an alias in
   libgcrypt for it, it does not harm to too much to return this other
   string.  Some users complained that we print "AES" but "AES192"
   and "AES256".  We can't fix that in libgcrypt but it is pretty
   safe to do it in an application. */
const char *
gnupg_cipher_algo_name (int algo)
{
  const char *s;

  s = gcry_cipher_algo_name (algo);
  if (!strcmp (s, "AES"))
    s = "AES128";
  return s;
}


/* Decide whether the filename is stdout or a real filename and return
 * an appropriate string.  */
const char *
print_fname_stdout (const char *s)
{
    if( !s || (*s == '-' && !s[1]) )
	return "[stdout]";
    return s;
}


/* Decide whether the filename is stdin or a real filename and return
 * an appropriate string.  */
const char *
print_fname_stdin (const char *s)
{
    if( !s || (*s == '-' && !s[1]) )
	return "[stdin]";
    return s;
}


static int
do_print_utf8_buffer (estream_t stream,
                      const void *buffer, size_t length,
                      const char *delimiters, size_t *bytes_written)
{
  const char *p = buffer;
  size_t i;

  /* We can handle plain ascii simpler, so check for it first. */
  for (i=0; i < length; i++ )
    {
      if ( (p[i] & 0x80) )
        break;
    }
  if (i < length)
    {
      int delim = delimiters? *delimiters : 0;
      char *buf;
      int ret;

      /*(utf8 conversion already does the control character quoting). */
      buf = utf8_to_native (p, length, delim);
      if (bytes_written)
        *bytes_written = strlen (buf);
      ret = es_fputs (buf, stream);
      xfree (buf);
      return ret == EOF? ret : (int)i;
    }
  else
    return es_write_sanitized (stream, p, length, delimiters, bytes_written);
}


void
print_utf8_buffer3 (estream_t stream, const void *p, size_t n,
                    const char *delim)
{
  do_print_utf8_buffer (stream, p, n, delim, NULL);
}


void
print_utf8_buffer2 (estream_t stream, const void *p, size_t n, int delim)
{
  char tmp[2];

  tmp[0] = delim;
  tmp[1] = 0;
  do_print_utf8_buffer (stream, p, n, tmp, NULL);
}


void
print_utf8_buffer (estream_t stream, const void *p, size_t n)
{
  do_print_utf8_buffer (stream, p, n, NULL, NULL);
}

/* Write LENGTH bytes of BUFFER to FP as a hex encoded string.
   RESERVED must be 0. */
void
print_hexstring (FILE *fp, const void *buffer, size_t length, int reserved)
{
#define tohex(n) ((n) < 10 ? ((n) + '0') : (((n) - 10) + 'A'))
  const unsigned char *s;

  (void)reserved;

  for (s = buffer; length; s++, length--)
    {
      putc ( tohex ((*s>>4)&15), fp);
      putc ( tohex (*s&15), fp);
    }
#undef tohex
}

char *
make_printable_string (const void *p, size_t n, int delim )
{
  return sanitize_buffer (p, n, delim);
}



/*
 * Check if the file is compressed.
 */
int
is_file_compressed (const char *s, int *ret_rc)
{
    iobuf_t a;
    byte buf[4];
    int i, rc = 0;
    int overflow;

    struct magic_compress_s {
        size_t len;
        byte magic[4];
    } magic[] = {
        { 3, { 0x42, 0x5a, 0x68, 0x00 } }, /* bzip2 */
        { 3, { 0x1f, 0x8b, 0x08, 0x00 } }, /* gzip */
        { 4, { 0x50, 0x4b, 0x03, 0x04 } }, /* (pk)zip */
    };

    if ( iobuf_is_pipe_filename (s) || !ret_rc )
        return 0; /* We can't check stdin or no file was given */

    a = iobuf_open( s );
    if ( a == NULL ) {
        *ret_rc = gpg_error_from_syserror ();
        return 0;
    }

    if ( iobuf_get_filelength( a, &overflow ) < 4 && !overflow) {
        *ret_rc = 0;
        goto leave;
    }

    if ( iobuf_read( a, buf, 4 ) == -1 ) {
        *ret_rc = a->error;
        goto leave;
    }

    for ( i = 0; i < DIM( magic ); i++ ) {
        if ( !memcmp( buf, magic[i].magic, magic[i].len ) ) {
            *ret_rc = 0;
            rc = 1;
            break;
        }
    }

leave:
    iobuf_close( a );
    return rc;
}


/* Try match against each substring of multistr, delimited by | */
int
match_multistr (const char *multistr,const char *match)
{
  do
    {
      size_t seglen = strcspn (multistr,"|");
      if (!seglen)
	break;
      /* Using the localized strncasecmp! */
      if (strncasecmp(multistr,match,seglen)==0)
	return 1;
      multistr += seglen;
      if (*multistr == '|')
	multistr++;
    }
  while (*multistr);

  return 0;
}



/* Parse the first portion of the version number S and store it at
   NUMBER.  On success, the function returns a pointer into S starting
   with the first character, which is not part of the initial number
   portion; on failure, NULL is returned.  */
static const char*
parse_version_number (const char *s, int *number)
{
  int val = 0;

  if (*s == '0' && digitp (s+1))
    return NULL; /* Leading zeros are not allowed.  */
  for (; digitp (s); s++ )
    {
      val *= 10;
      val += *s - '0';
    }
  *number = val;
  return val < 0? NULL : s;
}

/* Break up the complete string representation of the version number S,
   which is expected to have this format:

      <major number>.<minor number>.<micro number><patch level>.

   The major, minor and micro number components will be stored at
   MAJOR, MINOR and MICRO. On success, a pointer to the last
   component, the patch level, will be returned; on failure, NULL will
   be returned.  */
static const char *
parse_version_string (const char *s, int *major, int *minor, int *micro)
{
  s = parse_version_number (s, major);
  if (!s || *s != '.')
    return NULL;
  s++;
  s = parse_version_number (s, minor);
  if (!s || *s != '.')
    return NULL;
  s++;
  s = parse_version_number (s, micro);
  if (!s)
    return NULL;
  return s; /* Patchlevel.  */
}

/* Return true if version string is at least version B. */
int
gnupg_compare_version (const char *a, const char *b)
{
  int a_major, a_minor, a_micro;
  int b_major, b_minor, b_micro;
  const char *a_plvl, *b_plvl;

  if (!a || !b)
    return 0;

  /* Parse version A.  */
  a_plvl = parse_version_string (a, &a_major, &a_minor, &a_micro);
  if (!a_plvl )
    return 0; /* Invalid version number.  */

  /* Parse version B.  */
  b_plvl = parse_version_string (b, &b_major, &b_minor, &b_micro);
  if (!b_plvl )
    return 0; /* Invalid version number.  */

  /* Compare version numbers.  */
  return (a_major > b_major
          || (a_major == b_major && a_minor > b_minor)
          || (a_major == b_major && a_minor == b_minor
              && a_micro > b_micro)
          || (a_major == b_major && a_minor == b_minor
              && a_micro == b_micro
              && strcmp (a_plvl, b_plvl) >= 0));
}
