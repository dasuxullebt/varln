/** varln - variant symlinks under Linux
 * Copyright (C) 2016 Christoph-Simon Senjak
 *
 * This  program is  free  software; you  can  redistribute it  and/or
 * modify  it under the  terms of  the GNU  General Public  License as
 * published by the Free Software  Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A  PARTICULAR PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have  received a copy of the  GNU General Public License
 * along      with      this       program;      if      not,      see
 * <http://www.gnu.org/licenses/>.
 *
 */


#include <stdio.h> /* snprintf, sprintf */
#include <sys/types.h> /* open */
#include <sys/stat.h> /* open */
#include <fcntl.h> /* open */
#include <stdlib.h> /* malloc */
#include <errno.h>
#include <unistd.h> /* close, read */
#include <syslog.h>
#include <string.h> /* strerror */
#include <strings.h> /* index */
#include "envs.h"

const size_t INIT_STR = 8192;
const char* proc = "/proc/";
const char* environ = "/environ";

inline static int digits (pid_t pid) {
  return snprintf(0,0,"%+d",(int)pid)-1;
}

static char* actually_read_proc (int fd, size_t* n) {
  char* ret = (char*) malloc (INIT_STR);
  char *c = ret;
  int rsize = INIT_STR;
  if (ret) {
    int crd;
    while ((crd = read(fd, c, rsize)) > 0) {
      c += crd;
      rsize -= crd;
      if (rsize == 0) {
	char* nret = (char*) realloc (ret, 2*(c-ret));
	if (nret == NULL) {
	  syslog(LOG_DAEMON | LOG_CRIT, "Realloc failed! Out of memory (?)");
	  free(ret);
	  return NULL;
	} else {
	  rsize = c - ret;
	  c = nret + rsize;
	  nret = ret;
	}
      }
    }
    if (crd == 0) {
      /* EOF */
      *n = (c-ret);
      return ret;
    } else {
      /* FAIL */
      syslog(LOG_DAEMON | LOG_WARNING,
	     "Error while reading: \"%s\"", strerror(errno));
      free(ret);
      return NULL;
    }
  } else {
    syslog(LOG_DAEMON | LOG_CRIT, "Malloc failed! Out of memory (?)");
    return NULL;
  }
}

struct kv* parse_env (char* rd, size_t sz) {
  char* rd_end = rd + sz;
  char* cr;
  int n_args = 0;

  /* count the variables */
  for (cr = rd; cr < rd_end; cr++) if (*cr == '\0') n_args++;

  struct kv* ret = (struct kv*) malloc ((n_args + 1) * sizeof(struct kv));
  if (!ret) {
    syslog(LOG_DAEMON | LOG_CRIT, "Malloc failed! Out of memory (?)");
    return NULL;
  }

  /* notice: the following function makes assumptions about
     /proc/pid/...: It assumes that it has the *exact* format
     k1=v1\0k2=v2\0...\0. */
    
  cr = rd;
  int i;
  for (i = 0; i < n_args; ++i) {
    ret[i].key = cr;
    cr = index(cr, '=');
    *cr = '\0';
    ret[i].value = ++cr;
    cr = index(cr, '\0') + 1;
  }
  ret[n_args].key = ret[n_args].value = NULL;
  return ret;
}

/* the return value is a NULL-terminated array of kv
   (key=value=NULL). free this with free_kv.
 */
struct kv* read_proc (pid_t pid) {
  char filename[1+digits(pid)+sizeof(proc)+sizeof(environ)];
  sprintf(filename, "%s%d%s", proc, (int) pid, environ);
  int env_fd = open(filename, O_RDONLY);
  if (env_fd >= 0) {
    size_t sz;
    char* rd = actually_read_proc(env_fd, &sz);
    close(env_fd);
    if (!rd) return NULL;
    struct kv* ret = parse_env(rd, sz);
    if (!ret) {
      free(rd);
      return NULL;
    }
    return ret;
  } else {
    syslog(LOG_DAEMON | LOG_WARNING, "Could not open %s: \"%s\"",
	   filename, strerror(errno));
    return NULL;
  }
}

void free_kv (struct kv** k) {
  /* everything has originally been read into k->key, do these two
     calls are sufficient. */
  free((*k)->key);
  free(*k);
}

/* return the length of the value, or 0 if it was not found; truncate
   the contents, if the buffer was too small. */
ssize_t get_pid_env (pid_t p,
			    const char* name, size_t namesz,
			    char* buf, size_t bufsz) {
  __attribute__((cleanup(free_kv)))
    struct kv* k_orig = read_proc(p);
  struct kv* k = k_orig;
  if (k) {
    while (k->key != NULL) {
      if ((strlen(k->key) == namesz) &&
	  (strncmp(k->key, name, namesz) == 0)) {
	strncpy(buf, k->value, bufsz);
	return strlen(k->value);
      } else {
	k++;
      }
    }
  }
  return 0;
}
