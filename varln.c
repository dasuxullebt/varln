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

#define FUSE_USE_VERSION 25
#include <fuse.h>
#include <errno.h>
#include <string.h> /* strcmp, strlen */
#include <fcntl.h> /* open */
#include <sys/types.h> /* stat, open, opendir */
#include <sys/stat.h> /* stat, open */
#include <unistd.h> /* stat, readlink, chdir */
#include <sys/mman.h> /* mmap */
#include <syslog.h>
#include <limits.h> /* SSIZE_MAX */
#include <stdio.h> /* dprintf, sprintf */
#include <dirent.h> /* opendir, readdir */
#include <stdbool.h>

#include "envs.h"

char* confpath;

void* varln_init (struct fuse_conn_info *conn) {
  return NULL;
}

void varln_destroy (void* private_data) {
}

int varln_getattr (const char* _path, struct stat* stbuf) {
  syslog(LOG_DAEMON | LOG_DEBUG, "getattr on %s.", _path);

  if (strcmp("/", _path) == 0) {
    return stat(".", stbuf);
  }
  
  char path[strlen(confpath)+strlen(_path)+1];
  sprintf(path, "%s%s", confpath, _path);

  int r = stat(path, stbuf);
  if (!r) {
    if (S_ISDIR(stbuf->st_mode)) {
      /* we do not support directories */
      syslog(LOG_DAEMON | LOG_WARNING, "The file %s is a directory. "
	     "Only flat files may be in the config directory.", path);
      return -ENOENT;
    }
    stbuf->st_dev = 0;
    stbuf->st_ino = 0;
    stbuf->st_mode = 0120000 /* S_IFLNK */;
    return 0;
  }
  return -errno;
}

int varln_fgetattr (const char* path, struct stat* stbuf) {
  return varln_getattr(path, stbuf);
}

struct config_file {
  int fd;
  char* m;
  ssize_t size;
  bool isset; /* must be freed? */
};

ssize_t open_config_file (const char* path, struct config_file* cf) {
  syslog(LOG_DAEMON | LOG_DEBUG, "Opening configuration file %s.", path);
  cf->isset = false;
  cf->fd = open(path, O_RDONLY);
  if (cf->fd < 0) return -errno;
  struct stat st;
  if (fstat(cf->fd, &st)) {
    syslog(LOG_DAEMON | LOG_WARNING, "Could not stat file %s: %s", path, strerror(errno));
    return -ENOENT;
  } else if (S_ISDIR(st.st_mode)) {
    /* we do not support directories */
    syslog(LOG_DAEMON | LOG_WARNING, "The file %s is a directory. "
	   "Only flat files may be in the config directory.", path);
    close(cf->fd);
    return -ENOENT;
  } else if (st.st_size > sysconf(_SC_SSIZE_MAX)) {
    syslog(LOG_DAEMON | LOG_CRIT, "The config file %s is too large!", path);
    close(cf->fd);
    return -ENOENT;
  } else {
    cf->m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, cf->fd, 0);
    if (cf->m == MAP_FAILED) {
      syslog(LOG_DAEMON | LOG_WARNING, "Could not mmap file %s.", path);
      close(cf->fd);
      return -ENOENT;
    } else {
      cf->isset = true;
      return cf->size = st.st_size;
    }
  }
}

void close_config_file (struct config_file* cf) {
  if (cf->isset) {
    munmap(cf->m, cf->size);
    close(cf->fd);
  }
}

struct config_file_contents {
  const char* fallback_start;
  const char* fallback_end;
  const char* env_start;
  const char* env_end;
};

/* this function uses the path-argument just for log messages */
ssize_t parse_config_file (const char* c, const char* path, size_t sz,
			   struct config_file_contents* cf) {
  syslog(LOG_DAEMON | LOG_DEBUG, "Parsing configuration file %s.", path);
  if (sz == 0) {
    syslog(LOG_DAEMON | LOG_WARNING, "Config file %s is empty. Ignoring.", path);
    return -ENOENT;
  } else {
    cf->fallback_start = c;
    cf->fallback_end = memchr(c, '\n', sz);
    if (cf->fallback_end == NULL) {
      /* only fallback path is given. */
      cf->fallback_end = cf->fallback_start + sz;
      cf->env_start = NULL;
      cf->env_end = NULL;
      return 0;
    } else {
      cf->env_start = cf->fallback_end + 1;
      if ((cf->env_end = memchr(cf->env_start, '\n', sz - (cf->env_start - c))) == NULL) {
	/* no newline at the end of the file */
	cf->env_end = c + sz;
      }
      /* set the env to NULL when its the empty string */
      if (cf->env_start == cf->env_end) cf->env_start = cf->env_end = NULL;
      return 0;
    }
  }
}

int varln_readlink (const char* _path, char* buf, size_t size) {
  char path[strlen(confpath)+strlen(_path)+1];
  sprintf(path, "%s%s", confpath, _path);

  struct config_file cf;
  struct config_file_contents cfc;
  ssize_t r;
  int fd;
  char* m;

  if ((r = open_config_file(path, &cf)) < 0) goto unwind;
  if ((r = parse_config_file(cf.m, path, cf.size, &cfc)) != 0) goto unwind;

  if (cfc.env_start != NULL) {
    /* check environ variable */
    syslog(LOG_DAEMON | LOG_DEBUG, "Trying to get env.");
    struct fuse_context* cxt = fuse_get_context();
    ssize_t en = get_pid_env(cxt->pid, cfc.env_start,
			     cfc.env_end - cfc.env_start,
			     buf, size);
    if (en > 0) {
      if (en < size - 1) {
	buf[en] = '\0';
	r = 0;
	goto unwind;
      } else {
	r = -EFAULT;
      }
    }
  }
  /* environ variable empty or no environ variable in the config
     file: */
  syslog(LOG_DAEMON | LOG_WARNING, "Using fallback value.");
  ssize_t len = cfc.fallback_end - cfc.fallback_start;
  ssize_t cp = len < size ? len : size;
  memcpy(buf, cfc.fallback_start, cp);
  r = 0;
  goto unwind;

 unwind:
  close_config_file(&cf);
  return r;
}

void do_closedir (DIR** d) {
  if (*d) {
    closedir(*d);
  }
}

int varln_readdir (const char *_path, void *buf,
		   fuse_fill_dir_t filler, off_t offset,
		   struct fuse_file_info *fi) {
  if (strcmp("/", _path) == 0) {
    char path[strlen(confpath)+strlen(_path)+1];
    sprintf(path, "%s%s", confpath, _path);
    DIR* d __attribute__ ((__cleanup__(do_closedir))) =
      opendir(path);
    if (d == NULL) return -errno;
    errno = 0;
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
      char subpath[strlen(confpath)+strlen(_path)+strlen(de->d_name)+1];
      sprintf(subpath, "%s%s%s", confpath, _path, de->d_name);
      struct stat st;
      if (stat(subpath, &st) == 0) {
	if (! S_ISDIR(st.st_mode)) {
	  if (filler(buf, de->d_name, NULL, 0) != 0) return -ENOMEM;
	} else {
	  syslog(LOG_DAEMON | LOG_WARNING, "The file %s is a directory. "
		 "Only flat files may be in the config directory.", subpath);
	}
      } else {
	syslog(LOG_DAEMON | LOG_WARNING, "Stat for file %s failed: %s"
	       , subpath, strerror(errno));
      }
      errno = 0;
    }
    if (errno != 0) return -errno;
  } else {
    // not a directory
    return -EBADF;
  }
  return 0;
}

static struct fuse_operations varln_oper = {
  //  .init = varln_init,
  //  .destroy = varln_destroy,
  .getattr = varln_getattr,
  //  .fgetattr = varln_fgetattr,
  .readlink = varln_readlink,
  .readdir = varln_readdir,
};

int main (int argc, char** argv) {

  int i;
  

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-o") == 0) {
      i++; // ignore options
    } else {
      confpath = argv[i];
      int j;
      argc--;
      for (j = i; j < argc; ++j) {
	argv[j] = argv[j+1];
      }
      goto proceed;
    }
  }

 proceed:
  /* just as a test */
  if (chdir(confpath) < 0) {
    dprintf(STDERR_FILENO, "Could not chdir to config directory %s.", confpath);
    return -1;
  }

  openlog("varln:", LOG_NDELAY, LOG_DAEMON);
  
  return fuse_main(argc, argv, &varln_oper);
}
