/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 *
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* INTERNAL header, not installed.  The source's git description for a build
 * made from a release tarball, where there is no .git for the makefile to ask.
 *
 * .gitattributes marks this file export-subst, so git archive (and GitHub's
 * release tarballs) replace the $Format...$ below with the output of
 * git describe --tags.  In a checkout it stays a literal starting with '$',
 * and the makefile's -DDAS_GIT_DESC takes over.  See das_lib_version(). */

#ifndef _das_ver_p_h_
#define _das_ver_p_h_

#define DAS_GIT_ARCHIVE "$Format:%(describe:tags=true)$"

#endif /* _das_ver_p_h_ */
