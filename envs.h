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

#pragma once

struct kv {
  char* key;
  char* value;
};

struct kv* read_proc (pid_t pid);

void free_kv (struct kv** k);

ssize_t get_pid_env (pid_t p,
			    const char* name, size_t namesz,
			    char* buf, size_t bufsz);
