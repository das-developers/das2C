/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 *
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* das_fetch: walk the das federated catalog, describe data sources, and
 * stream their output to stdout, all by catalog path.  Nobody types a
 * server URL.
 *
 * Everything that understands a catalog or a data source is the library's
 * job: DasCat (das3/catalog.h) resolves paths, DasSrc (das3/source.h) turns
 * a source node's "interface" section into keys and a DasReq (das3/req.h)
 * of KEY=VALUE settings into the HTTP GET URL the "protocol" section calls
 * for.  What lives here is the command line spelling of all that: the
 * options for where paths start, the example shortcuts, and the listings.
 *
 * The body of a data response is never inspected.  It may be a das2 or das3
 * stream, a CDF file, CSV text, or CCSDS packets.  Bytes are bytes.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include <das3/core.h>
#include <das3/node.h>
#include <das3/catalog.h>
#include <das3/source.h>
#include <das3/req.h>
#include <das3/http.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#define strncasecmp _strnicmp      /* MSVC keeps the POSIX one under its own name */
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <strings.h>               /* strncasecmp */
#endif

#define PROG "das_fetch"
#define PERR (DASERR_MAX + 1)

#define CRED_FILE     "servers"    /* under das_userdir(); the older ~/.dasauth is not read */

#ifndef _WIN32
#define HOME_VAR "$HOME"           /* for help text, das_userdir() does the lookup */
#else
#define HOME_VAR "%USERPROFILE%"
#endif

/* Exit codes.  Documented in the help text so shell scripts can branch on
 * them instead of parsing stderr. */
#define RET_OK     0
#define RET_USAGE  2
#define RET_NODE   3
#define RET_QUERY  4
#define RET_SERVER 5
#define RET_AUTH   6
#define RET_WRITE  7

/* Command line bounds.  Identifiers in a node follow the das3 rule enforced
 * by das_assert_valid_id(), at most 63 characters, and a full key is
 * "section.group.prop", so KEY_SZ comes from that rule, not from a guess. */
#define MAX_KEYS 512    /* KEY=VALUE arguments on one command line */
#define MAX_RAW   32    /* -p arguments */
#define KEY_SZ   (8 + 2*DAS_MAX_ID_BUFSZ)
#define NAME_SZ  DAS_MAX_ID_BUFSZ
#define URI_SZ 1024
#define URL_SZ 4096
#define ERR_SZ 1024

#define START_ENV "DASFETCH_START"
#define MOUNT_ENV "DASFETCH_MOUNT"
#define MAX_MOUNTS 16
#define MAX_KIDS 32

/* What to take when a catalog node offers several sources.  das3 streams
 * are what the rest of the das3 tools read; anything else is still
 * better than nothing for a program that only copies bytes to stdout. */
#define DEF_ACCEPT "application/vnd.das.stream"

/* ************************************************************************* */
/* Help */

static void prnHelpMain(void)
{
	fputs(
"NAME\n"
"   " PROG " - fetch data by das catalog path, no server URLs required\n"
"\n"
"USAGE\n"
"   " PROG " [options] seeds\n"
"   " PROG " [options] node [PATH]\n"
"   " PROG " [options] json PATH[#FRAGMENT]\n"
"   " PROG " [options] url  PATH [KEY=VALUE ...]\n"
"   " PROG " [options] data PATH [KEY=VALUE ...]  > out_file\n"
"   " PROG " COMMAND -h\n"
"\n"
"DESCRIPTION\n"
"   The das federated catalog is a tree of small JSON documents.  Catalog\n"
"   nodes list other nodes, and data source nodes describe how to query a\n"
"   server.  " PROG " walks that tree so you never have to know where a\n"
"   server lives or what its query string looks like.\n"
"\n"
"   Interactive use is 'node' to look around, then 'data' to download.  In a\n"
"   script, 'data' writes the response body to standard output, which can be\n"
"   redirected to a file or piped to das3_csv, das2_psd and friends.  The\n"
"   body is passed through untouched, so the output format is whatever the\n"
"   source produces: das2 or das3 stream, CDF, CSV, or raw packets.\n"
"\n"
"COMMANDS\n"
"   seeds            Print the compiled in catalog seed URLs.\n"
"   node [PATH]      List a catalog's children, or a data source's examples\n"
"                    and query keys.  No PATH shows where paths start.\n"
"   json PATH        Print a node's raw JSON, or a fragment of it.\n"
"   url  PATH ...    Print the HTTP GET URL a query turns into.\n"
"   data PATH ...    Run the query, write the response body to stdout.\n"
"\n"
"   Each command has a help page: '" PROG " data -h' explains query keys,\n"
"   '" PROG " node -h' explains the listings.\n"
"\n"
"PATHS\n"
"   Paths are catalog paths, not web paths.  Any of these forms work:\n"
"\n"
"      uiowa/juno/wav/survey             relative to the prefix, by\n"
"                                        default '" DASCAT_DEF_PREFIX "'\n"
"      site:/uiowa/juno/wav/survey       shorthand for the URI form below\n"
"      tag:das2.org,2012:site:/uiowa     full catalog path URI\n"
"      https://host/some/node.json       a node by direct URL\n"
"      ./local_node.json                 a node file on disk\n"
"\n"
"OPTIONS\n"
"   -h, --help         This text, or with a command, that command's help.\n"
"   -V, --version      Print the program and library version and exit.\n"
"\n"
"   -r, --start START  Where relative paths start, in one of three forms.\n"
"                      A catalog URI prefix, default\n"
"                      '" DASCAT_DEF_PREFIX "':\n"
"                         -r tag:das2.org,2012:test:/\n"
"                      A URL or file, the node there becomes the seed, a\n"
"                      root: paths are relative to it, names below it are\n"
"                      paths from it, and nothing outside it can be\n"
"                      reached:\n"
"                         -r ./my_site_copy.json\n"
"                      URL=URI, the same with the node declared to be the\n"
"                      catalog node URI, so that saved requisitions match\n"
"                      the node the file stands in for.  Any -m mounts join\n"
"                      a seed to make an offline catalog:\n"
"                         -r ./my_site_copy.json=site:\n"
"                         -r https://doi.org/10.1037/a0040251/root.json=10.1037/a0040251\n"
"                      Without -r the environment variable " START_ENV " is\n"
"                      used the same way, if set.\n"
"   -m, --mount URL=NAME\n"
"                      Mount a node: the node at URL stands in for NAME and\n"
"                      everything under it, the rest of the catalog is\n"
"                      unchanged.  NAME is a catalog URI, or with a -r URL\n"
"                      seed, a path from it.  A mount does not move where\n"
"                      relative paths start:\n"
"                         -m https://tracers-dev.physics.uiowa.edu/stream/source/tracers-1.json=\\\n"
"                            site:/uiowa/tracers/tracers-1\n"
"                      May be repeated.  The environment variable " MOUNT_ENV "\n"
"                      stands in for -m, if set.\n"
"\n"
"   -s SECTIONS        For 'node', which parts of a source to print, a comma\n"
"                      separated list from: examples, coords, data, formats,\n"
"                      options, all.  The default leaves out formats.\n"
"   -p PARAM=VALUE     Send a raw HTTP GET parameter, bypassing the interface\n"
"                      translation.  Repeatable.\n"
"   -a TYPES           Which source to use when a catalog node offers more\n"
"                      than one: a comma separated list of the media types\n"
"                      you can read.  Order means nothing, any source that\n"
"                      provides one of them qualifies, and */* is anything.\n"
"                      Without -a, das3 streams are asked for first and\n"
"                      then anything, as if by:\n"
"                         -a '" DEF_ACCEPT "'  then  -a '*/*'\n"
"                      If several sources remain, put the name of one on\n"
"                      the end of the path.\n"
"\n"
"   -f                 Fresh: ignore cached catalog nodes along the path and\n"
"                      fetch them again.  Use after a catalog has changed.\n"
"   -C DIR             Keep cached catalog nodes in DIR instead of the\n"
"                      default below, or 'none' for no cache at all.\n"
"\n"
"   -t SECONDS         Connection timeout for the data request, default 10.\n"
"   -q                 Quiet, only errors on stderr.\n"
"   -v                 Verbose: node and server URLs, all source sections,\n"
"                      the arguments behind each example, usage hints, and\n"
"                      a report of each URL contacted.\n"
"   -l LEVEL           Log level: crit, error, warn, info, debug, trace.\n"
"\n"
"FILES\n"
"   " HOME_VAR "/.das/cache/nodes/\n"
"      Catalog nodes fetched over HTTP, kept for an hour.  Walking a catalog\n"
"      costs one request per level, so repeat runs only talk to the data\n"
"      server.  See -f and -C.\n"
"   " HOME_VAR "/.das/" CRED_FILE "\n"
"      Saved credentials.  When a source needs them these are tried before\n"
"      the username and password prompt.\n"
"   The environment variable DAS_USERDIR replaces " HOME_VAR "/.das for both.\n"
"\n"
"EXIT CODES\n"
"   0 success    2 usage error      3 path did not resolve   4 bad query\n"
"   5 network or server failure     6 authentication failed  7 write failed\n"
"\n"
"SEE ALSO\n"
"   das3_csv, das3_text, das3_info, das2_psd\n"
"\n"
"   https://das2.org, and 'json PATH' to see a node as the server sent it.\n"
"\n", stdout);
}

static void prnHelpNode(void)
{
	fputs(
"USAGE\n"
"   " PROG " [options] node [PATH]\n"
"\n"
"   Show what is at a catalog path.  With no PATH, where paths start, which\n"
"   lists the federated sites.\n"
"\n"
"CATALOGS\n"
"   A catalog node prints its name and URI, then one line per child:\n"
"   name, node type, and title.  Append a child's name to the path to step\n"
"   down.  The URI line ends with the separator this catalog uses, so the\n"
"   next path is URI plus child name.  A '*' after the type marks node\n"
"   types " PROG " can list but not load or fetch from (Das2DSDF, WebSocSrc).\n"
"\n"
"   A SourceSet or Collection holds several descriptions of one dataset.\n"
"   Only the one 'data' and 'url' would use is shown, chosen by what it\n"
"   provides against the -a list.  -v lists them all.  To use another,\n"
"   add its name to the path, as in uiowa/juno/wav/survey/das2.\n"
"\n"
"DATA SOURCES\n"
"   An HttpStreamSrc node prints a header, its examples, then one table per\n"
"   interface section.\n"
"\n"
"   examples      Key is the number to give 'data' or 'url' to run that\n"
"                 example.  With -v the KEY=VALUE arguments behind each one\n"
"                 are shown too.\n"
"   coords        Coordinate properties, such as the time range.\n"
"   data          Data variables that can be switched on or off.\n"
"   formats       Output format groups and their settings.  Hidden by\n"
"                 default, -s formats or -v shows them.  The header line\n"
"                 'Formats:' always lists the group names.\n"
"   options       Source options that fit nowhere else.\n"
"\n"
"   Table columns: Key is what you type, without the section prefix; Type\n"
"   and Units are the value's; Default is what the server uses when the key\n"
"   is not given; Req marks keys the server insists on; Values lists an\n"
"   enum's choices or a coordinate's range.\n"
"\n"
"OPTIONS\n"
"   -s SECTIONS   Print only these sections, comma separated: examples,\n"
"                 coords, data, formats, options, all.\n"
"   -v            Also print the node and server URLs, all sections, each\n"
"                 example's arguments, and usage hints.\n"
"\n"
"EXAMPLES\n"
"   " PROG " node\n"
"   " PROG " node uiowa/juno/wav\n"
"   " PROG " node -s examples,coords uiowa/tracers/tracers-2/spice/geomagcoords\n"
"   " PROG " -r https://tracers-dev.physics.uiowa.edu/stream/root.json=site:/uiowa/tracers node\n"
"\n", stdout);
}

static void prnHelpQuery(bool bData)
{
	fprintf(stdout,
"USAGE\n"
"   " PROG " [options] %s PATH [KEY=VALUE ...]\n"
"\n", bData ? "data" : "url ");
	if(bData)
		fputs(
"   Resolve the query, request it, and write the response body to stdout\n"
"   untouched, so the output is whatever the source produces.  Diagnostics\n"
"   go to stderr.  A password prompt, if the source needs one, is read from\n"
"   the terminal, not stdin.  Exit code 0 means the whole body arrived.\n"
"\n", stdout);
	else
		fputs(
"   Resolve the query and print the HTTP GET URL that 'data' would request,\n"
"   without requesting it.  For scripts, and for checking what a KEY=VALUE\n"
"   pair turns into.\n"
"\n", stdout);
	fputs(
"QUERY KEYS\n"
"   Keys come from the source's interface definition, so they differ from\n"
"   source to source.  Run 'node PATH' to see them.  They look like:\n"
"\n"
"      coords.time.min=2026-03-01    a coordinate property\n"
"      data.lshell.enabled=true      a data variable property\n"
"      formats.csv.delimiter=Tab     an output format property\n"
"      options.decompress=true       a source option\n"
"\n"
"   Shortcuts, as long as the result is unambiguous:\n"
"\n"
"      time.min=2026-03-01           drop the section prefix\n"
"      time=2026-03-01,2026-03-02    coordinate range MIN,MAX[,STEP].  STEP\n"
"                                    is the resolution if the coordinate has\n"
"                                    one, otherwise the interval.  Leave an\n"
"                                    element empty to skip it: time=,2026-03-02\n"
"      lshell=true  or just  lshell  toggle a data variable or boolean option\n"
"      csv  or  formats.csv          select an output format group\n"
"      1  or  examples.1             start from the maintainer's example 1,\n"
"                                    then apply the other keys on top\n"
"      min / minimum, max / maximum, res / resolution, inter / interval are\n"
"                                    interchangeable property names\n"
"\n"
"   With no keys at all, example 0 runs, so a bare 'data PATH' always\n"
"   produces something to look at.\n"
"\n"
"   Keys set to their default value are not sent to the server, except for\n"
"   parameters the server requires.  Required parameters without a default\n"
"   must be given or the request is refused locally with a message naming\n"
"   the key to set.  Nothing in these rules assumes the first coordinate is\n"
"   time; the coordinate names are whatever the source declares.\n"
"\n"
"   A source that offers both stream versions has a key for it, usually\n"
"   version=das2 or version=das3; its default is what the server sends when\n"
"   you say nothing.\n"
"\n"
"OPTIONS\n"
"   -p PARAM=VALUE   Send a raw HTTP GET parameter, bypassing the interface\n"
"                    translation.  Repeatable.  Ties your script to one\n"
"                    server's API, so prefer KEY=VALUE when you can.\n"
"   -t SECONDS       Connection timeout, default 10.\n"
"\n"
"EXAMPLES\n"
"   A day of Juno Waves survey data:\n"
"\n"
"      " PROG " data uiowa/juno/wav/survey time=2021-02-21,2021-02-22 \\\n"
"         > jno_wav_2021-02-21.d2s\n"
"\n"
"   A source's first example, just to see that it works:\n"
"\n"
"      " PROG " data uiowa/tracers/tracers-2/spice/geomagcoords | das3_text -3\n"
"\n"
"   Predicted TRACERS-2 magnetic coordinates, one point a minute, as CSV:\n"
"\n"
"      " PROG " data uiowa/tracers/tracers-2/spice/geomagcoords \\\n"
"         time=2026-04-01,2026-04-01T01:30,60 csv\n"
"\n"
"   One housekeeping value from a development server, through das3_csv:\n"
"\n"
"      " PROG " -r https://tracers-dev.physics.uiowa.edu/stream/root.json=site:/uiowa/tracers \\\n"
"         data tracers-2/ql/mag/hsk time=2026-01-01,2026-01-02 sprt | das3_csv\n"
"\n"
"   The URL a query turns into, without running it:\n"
"\n"
"      " PROG " url uiowa/juno/wav/survey time=2021-02-21,2021-02-22 b\n"
"\n", stdout);
}

static void prnHelpJson(void)
{
	fputs(
"USAGE\n"
"   " PROG " [options] json PATH[#FRAGMENT]\n"
"\n"
"   Print the node's JSON document, pretty printed, or just the part at\n"
"   FRAGMENT, a slash separated path into it.  For looking at what the\n"
"   catalog really says when a listing leaves you wondering.\n"
"\n"
"EXAMPLES\n"
"   " PROG " json uiowa/juno/wav/survey\n"
"   " PROG " json 'uiowa/tracers/tracers-2/spice/geomagcoords#protocol/httpParams'\n"
"   " PROG " json 'uiowa/tracers/tracers-2/spice/geomagcoords#interface/examples'\n"
"\n", stdout);
}

static void prnHelpSeeds(void)
{
	fputs(
"USAGE\n"
"   " PROG " seeds\n"
"\n"
"   Print the compiled in URLs of the das catalog index, one per line.\n"
"   The index is mirrored, the first URL that answers is used.  A -r START\n"
"   argument or the " START_ENV " variable replaces it, a -m URL=URI argument\n"
"   or the " MOUNT_ENV " variable mounts a node on it, see '" PROG " -h'.\n"
"\n", stdout);
}

/* Help for a command, or the overview */
static void prnHelp(const char* sCmd)
{
	if(sCmd == NULL) prnHelpMain();
	else if((strcmp(sCmd, "node") == 0)||(strcmp(sCmd, "ls") == 0)) prnHelpNode();
	else if((strcmp(sCmd, "data") == 0)||(strcmp(sCmd, "get") == 0)) prnHelpQuery(true);
	else if(strcmp(sCmd, "url") == 0) prnHelpQuery(false);
	else if(strcmp(sCmd, "json") == 0) prnHelpJson();
	else if(strcmp(sCmd, "seeds") == 0) prnHelpSeeds();
	else prnHelpMain();
}

/* ************************************************************************* */
/* Small string helpers */

static bool strieq(const char* a, const char* b)
{
	if((a == NULL)||(b == NULL)) return false;
	while(*a && *b){
		if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
		++a; ++b;
	}
	return (*a == '\0') && (*b == '\0');
}

static void strcpyz(char* sDest, const char* sSrc, size_t uDest)
{
	if(uDest == 0) return;
	if(sSrc == NULL){ sDest[0] = '\0'; return; }
	strncpy(sDest, sSrc, uDest - 1);
	sDest[uDest - 1] = '\0';
}

static bool isDigits(const char* s)
{
	if((s == NULL)||(*s == '\0')) return false;
	for(; *s; ++s) if(!isdigit((unsigned char)*s)) return false;
	return true;
}

/* A string element of a node document, or NULL.  Only the listings read
 * documents directly, everything about a source comes through DasSrc. */
static const char* jstr(const DasJdo* p, const char* sPath)
{
	return (p == NULL) ? NULL : DasJdo_string(DasJdo_get(p, sPath));
}

/* Section names as typed in -s lists, singular or plural */
static bool secAlias(const char* s, das_src_sec_e* pSec)
{
	if(strieq(s,"coord")||strieq(s,"coords")||strieq(s,"coordinate")||strieq(s,"coordinates")){
		*pSec = das_src_sec_coord; return true;
	}
	if(strieq(s,"data")){ *pSec = das_src_sec_data; return true; }
	if(strieq(s,"format")||strieq(s,"formats")||strieq(s,"fmt")){ *pSec = das_src_sec_format; return true; }
	if(strieq(s,"option")||strieq(s,"options")||strieq(s,"opt")||strieq(s,"opts")){
		*pSec = das_src_sec_option; return true;
	}
	return false;
}

/* ************************************************************************* */
/* Display settings and terminal wrapping */

/* Which parts of a source 'node' prints: one bit per section plus examples */
#define SHOW_EXAMPLES (1u << das_src_sec_size)
#define SHOW_ALL      ((1u << (das_src_sec_size + 1)) - 1)
static unsigned g_uShow = SHOW_ALL & ~(1u << das_src_sec_format);
static bool g_bVerbose = false;
static int  g_nCols = 0;    /* terminal width, 0 when stdout is not a terminal */

/* Display width of UTF-8 text, code points not bytes.  Catalog text has no
 * double width characters in practice, so this is as far as it goes. */
static size_t u8width(const char* s, size_t uBytes)
{
	size_t w = 0;
	for(size_t u = 0; u < uBytes; ++u) if((((unsigned char)s[u]) & 0xC0) != 0x80) ++w;
	return w;
}

/* Print text that starts at column nCol, breaking on spaces at the terminal
 * width with continuation lines indented back to nCol.  Without a terminal,
 * or with too little room to be worth it, the text goes out as is. */
static void fputsWrappedAt(const char* sText, int nCol, int nWidth, FILE* pOut)
{
	int nAvail = nWidth - nCol;
	if((nWidth <= 0)||(nAvail < 20)||(u8width(sText, strlen(sText)) <= (size_t)nAvail)){
		fputs(sText, pOut);
		return;
	}
	const char* p = sText;
	bool bFirst = true;
	while(*p != '\0'){
		while(*p == ' ') ++p;
		if(*p == '\0') break;
		const char* pEnd = p;
		const char* pBreak = NULL;
		size_t w = 0;
		while((*pEnd != '\0') && (w < (size_t)nAvail)){
			if(*pEnd == ' ') pBreak = pEnd;
			if((((unsigned char)*pEnd) & 0xC0) != 0x80) ++w;
			++pEnd;
		}
		if((*pEnd == '\0')||(*pEnd == ' ')) pBreak = pEnd;  /* fits, or breaks right here */
		else if(pBreak == NULL) pBreak = pEnd;              /* one long word, cut it */
		if(!bFirst){
			fputc('\n', pOut);
			for(int i = 0; i < nCol; ++i) fputc(' ', pOut);
		}
		fwrite(p, 1, (size_t)(pBreak - p), pOut);
		bFirst = false;
		p = pBreak;
	}
}

static void fputsWrapped(const char* sText, int nCol, FILE* pOut)
{
	fputsWrappedAt(sText, nCol, g_nCols, pOut);
}

/* A paragraph of explanation, wrapped to the terminal, or to 80 columns
 * when there is none so piped output does not get one endless line.  An
 * optional indent applies to every line. */
static void prnPara(FILE* pOut, int nIndent, const char* sText)
{
	for(int i = 0; i < nIndent; ++i) fputc(' ', pOut);
	fputsWrappedAt(sText, nIndent, (g_nCols > 0) ? g_nCols : 80, pOut);
	fputc('\n', pOut);
}

/* A labeled header line, wrapped to the terminal */
static void prnField(FILE* pOut, const char* sLabel, const char* sText)
{
	if((sText == NULL)||(sText[0] == '\0')) return;
	fprintf(pOut, "%-10s", sLabel);
	fputsWrapped(sText, 10, pOut);
	fputc('\n', pOut);
}

/* Fixed properties are shown dimmed under -v so the eye goes to what can be
 * set.  Only on a terminal; a pipe gets plain text and the Req column
 * says "fixed" either way. */
static const char* dimOn(void){  return (g_nCols > 0) ? "\033[2m" : ""; }
static const char* dimOff(void){ return (g_nCols > 0) ? "\033[0m" : ""; }

/* ************************************************************************* */
/* Describing a source.  The library has the facts, these functions only
 * decide what goes in which column. */

/* The node's spelling of a type when it gave one, else a name for what
 * the library inferred */
static const char* propTypeName(const DasSrcProp* p)
{
	const char* s = DasSrcProp_typeName(p);
	if((s != NULL)&&(s[0] != '\0')) return s;
	switch(DasSrcProp_type(p)){
	case das_srcprop_bool: return "boolean";
	case das_srcprop_int:  return "integer";
	case das_srcprop_real: return "real";
	case das_srcprop_time: return "isotime";
	case das_srcprop_enum: return "enum";
	default:               return "string";
	}
}

/* Allowed values for display: enum entries, a range, or true/false */
static void propValues(const DasSrcProp* p, char* sBuf, size_t uLen)
{
	sBuf[0] = '\0';
	const DasSrcCtrl* pC = DasSrcProp_ctrl(p);
	if(pC == NULL) return;

	size_t uEnum = DasSrcCtrl_enumCount(pC);
	if(uEnum > 0){
		size_t u = 0;
		for(size_t i = 0; i < uEnum; ++i){
			const char* sVal = DasSrcCtrl_enum(pC, i);
			if(u + strlen(sVal) + 3 >= uLen){ strcpyz(sBuf + u, "...", uLen - u); return; }
			u += snprintf(sBuf + u, uLen - u, "%s%s", (u > 0) ? ", " : "", sVal);
		}
		return;
	}
	const char* sLo = "";
	const char* sHi = "";
	if(DasSrcCtrl_range(pC, &sLo, &sHi)){
		snprintf(sBuf, uLen, "%s to %s", sLo, sHi);
		return;
	}
	if(DasSrcProp_type(p) == das_srcprop_bool) strcpyz(sBuf, "true, false", uLen);
}

/* Print the arguments that reproduce an example, indented under its row.
 * The library already has them in interface terms, whatever the node
 * layout, so this is a requisition read back as KEY=VALUE pairs. */
static void printExampleArgs(const DasSrc* pSrc, size_t uIdx, int nCol, FILE* pOut)
{
	DasReq* pReq = DasSrc_newReq(pSrc, (int)uIdx);
	if(pReq == NULL) return;
	char sArgs[4096] = {'\0'};
	size_t u = 0;
	for(size_t i = 0; (i < DasReq_count(pReq)) && (u < sizeof(sArgs)); ++i)
		u += snprintf(sArgs + u, sizeof(sArgs) - u, "%s=%s ", DasReq_key(pReq, i), DasReq_val(pReq, i));
	for(size_t i = 0; (i < DasReq_rawCount(pReq)) && (u < sizeof(sArgs)); ++i){
		const char* sVal = NULL;
		const char* sName = DasReq_raw(pReq, i, &sVal);
		u += snprintf(sArgs + u, sizeof(sArgs) - u, "-p %s=%s ", sName, sVal);
	}
	del_DasReq(pReq);

	for(int i = 0; i < nCol; ++i) fputc(' ', pOut);
	fputsWrapped(sArgs, nCol, pOut);
	fputs("\n", pOut);
}

/* The examples table: number and what it provides, arguments only with -v */
static void printExamples(const DasSrc* pSrc, FILE* pOut)
{
	size_t uEx = DasSrc_exampleCount(pSrc);
	if(uEx == 0) return;
	size_t wKey = 3;
	char sNum[16];
	for(size_t u = 0; u < uEx; ++u){
		snprintf(sNum, sizeof(sNum), "%zu", u);
		if(strlen(sNum) > wKey) wKey = strlen(sNum);
	}
	fprintf(pOut, "\nexamples:\n  %-*s  Provides\n  ", (int)wKey, "Key");
	for(size_t i = 0; i < wKey; ++i) fputc('-', pOut);
	fputs("  --------\n", pOut);
	int nCol = 2 + (int)wKey + 2;
	for(size_t u = 0; u < uEx; ++u){
		fprintf(pOut, "  %-*zu  ", (int)wKey, u);
		fputsWrapped(DasSrc_exampleLabel(pSrc, u), nCol, pOut);
		fputc('\n', pOut);
		if(g_bVerbose) printExampleArgs(pSrc, u, nCol, pOut);
	}
}

/* The realm and methods are not modeled by DasSrc, they come from the
 * protocol section as the node wrote it.  Old nodes say "authentication". */
static void prnAuth(const DasSrc* pSrc, FILE* pOut)
{
	const DasJdo* pAuth = DasSrc_jdo(pSrc, "protocol/authorization");
	if(pAuth == NULL) pAuth = DasSrc_jdo(pSrc, "protocol/authentication");
	if(pAuth == NULL){ fputs("Auth:     unknown\n", pOut); return; }
	if(!DasSrc_needsAuth(pSrc)){ fputs("Auth:     not required\n", pOut); return; }
	fputs("Auth:     REQUIRED", pOut);
	if(jstr(pAuth, "realm")) fprintf(pOut, ", realm \"%s\"", jstr(pAuth, "realm"));
	const DasJdo* pMeth = DasJdo_get(pAuth, "methods");
	if((pMeth != NULL)&&(pMeth->type == das_json_type_ary)){
		fputs(" via", pOut);
		for(const das_json_ary_el* pEl = DasJdo_aryFirst(pMeth); pEl; pEl = pEl->next)
			fprintf(pOut, " %s", DasJdo_string(pEl->value) ? DasJdo_string(pEl->value) : "?");
	}
	if(jstr(pAuth, "description")) fprintf(pOut, "\n          %s", jstr(pAuth, "description"));
	fputs("\n", pOut);
}

/* The format groups by name, or for a source without any the one MIME
 * type it produces */
static void prnFormats(const DasSrc* pSrc, FILE* pOut)
{
	size_t uGroups = DasSrc_groupCount(pSrc, das_src_sec_format);
	if(uGroups > 0){
		fputs("Formats: ", pOut);
		for(size_t g = 0; g < uGroups; ++g){
			const DasSrcGroup* pG = DasSrc_group(pSrc, das_src_sec_format, g);
			fprintf(pOut, " %s", DasSrcGroup_name(pG));
			const DasSrcProp* pOn = DasSrcGroup_enable(pG);
			if((pOn != NULL) && strieq(DasSrcProp_value(pOn), "true")) fputs(" (default)", pOut);
		}
		fputs("\n", pOut);
		return;
	}
	const char* sMime = DasSrc_mime(pSrc, NULL);
	if(sMime[0] != '\0') fprintf(pOut, "Formats:  %s\n", sMime);
}

/* One table per section.  Keys are shown without their section prefix
 * since the heading carries it.  Column widths are per section so the long
 * enum lists of the formats section do not push everything else over.
 * Fixed properties, the ones with no control, only appear under -v. */
static void printKeyTables(const DasSrc* pSrc, FILE* pOut)
{
	enum { C_KEY, C_TYPE, C_UNITS, C_DEF, C_REQ, C_VALS, C_DESC, C_COUNT };
	const char* aHdr[C_COUNT] = {"Key", "Type", "Units", "Default", "Req", "Values", "Description"};
	char sVals[80];
	char sTitle[64];

	for(int iSec = 0; iSec < das_src_sec_size; ++iSec){
		das_src_sec_e sec = (das_src_sec_e)iSec;
		if(!(g_uShow & (1u << sec))) continue;

		/* First pass: is there anything to show, and how wide is it */
		size_t aW[C_COUNT];
		for(int c = 0; c < C_COUNT; ++c) aW[c] = strlen(aHdr[c]);
		size_t uRows = 0;
		for(size_t g = 0; g < DasSrc_groupCount(pSrc, sec); ++g){
			const DasSrcGroup* pG = DasSrc_group(pSrc, sec, g);
			for(size_t i = 0; i < DasSrcGroup_propCount(pG); ++i){
				const DasSrcProp* p = DasSrcGroup_prop(pG, i);
				if((DasSrcProp_ctrl(p) == NULL) && !g_bVerbose) continue;
				++uRows;
				const char* sKey = DasSrcProp_key(p);
				const char* sShort = strchr(sKey, '.') ? strchr(sKey, '.') + 1 : sKey;
				const char* sDef = DasSrcProp_value(p);
				propValues(p, sVals, sizeof(sVals));
				size_t a[C_COUNT] = {
					strlen(sShort), strlen(propTypeName(p)), strlen(DasSrcProp_units(p)),
					sDef ? strlen(sDef) : 0, DasSrcProp_ctrl(p) ? 3 : 5, strlen(sVals), 0
				};
				for(int c = 0; c < C_DESC; ++c) if(a[c] > aW[c]) aW[c] = a[c];
			}
		}
		if(uRows == 0) continue;
		int nDescCol = 2;
		for(int c = 0; c < C_DESC; ++c) nDescCol += (int)aW[c] + 2;

		fprintf(pOut, "\n%s:\n", das_src_sec_name(sec));
		fprintf(pOut, "  %-*s  %-*s  %-*s  %-*s  %-*s  %-*s  %s\n",
		        (int)aW[C_KEY], aHdr[C_KEY], (int)aW[C_TYPE], aHdr[C_TYPE],
		        (int)aW[C_UNITS], aHdr[C_UNITS], (int)aW[C_DEF], aHdr[C_DEF],
		        (int)aW[C_REQ], aHdr[C_REQ], (int)aW[C_VALS], aHdr[C_VALS], aHdr[C_DESC]);
		fputs("  ", pOut);
		for(int c = 0; c < C_COUNT; ++c){
			size_t w = (c == C_DESC) ? 24 : aW[c];
			for(size_t i = 0; i < w; ++i) fputc('-', pOut);
			if(c < C_DESC) fputs("  ", pOut);
		}
		fputs("\n", pOut);

		for(size_t g = 0; g < DasSrc_groupCount(pSrc, sec); ++g){
			const DasSrcGroup* pG = DasSrc_group(pSrc, sec, g);
			for(size_t i = 0; i < DasSrcGroup_propCount(pG); ++i){
				const DasSrcProp* p = DasSrcGroup_prop(pG, i);
				const DasSrcCtrl* pC = DasSrcProp_ctrl(p);
				if((pC == NULL) && !g_bVerbose) continue;
				const char* sKey = DasSrcProp_key(p);
				const char* sShort = strchr(sKey, '.') ? strchr(sKey, '.') + 1 : sKey;
				const char* sDef = DasSrcProp_value(p);
				const char* sReq = (pC == NULL) ? "fixed" : DasSrcCtrl_required(pC) ? "yes" : "";
				propValues(p, sVals, sizeof(sVals));
				if(pC == NULL) fputs(dimOn(), pOut);
				fprintf(pOut, "  %-*s  %-*s  %-*s  %-*s  %-*s  %-*s  ",
				        (int)aW[C_KEY], sShort, (int)aW[C_TYPE], propTypeName(p),
				        (int)aW[C_UNITS], DasSrcProp_units(p), (int)aW[C_DEF], sDef ? sDef : "",
				        (int)aW[C_REQ], sReq, (int)aW[C_VALS], sVals);
				fputsWrapped(DasSrcProp_title(p, sTitle, sizeof(sTitle)), nDescCol, pOut);
				if(pC == NULL) fputs(dimOff(), pOut);
				fputc('\n', pOut);
			}
		}
	}
}

/* Shortcut hints specific to this source: the coordinates that take a
 * MIN,MAX[,STEP] range */
static void printShortcuts(const DasSrc* pSrc, FILE* pOut)
{
	fputs("\nShortcuts:\n", pOut);
	for(size_t g = 0; g < DasSrc_groupCount(pSrc, das_src_sec_coord); ++g){
		const DasSrcGroup* pG = DasSrc_group(pSrc, das_src_sec_coord, g);
		const DasSrcProp* pMin = DasSrcGroup_findProp(pG, "min");
		const DasSrcProp* pMax = DasSrcGroup_findProp(pG, "max");
		if((pMin == NULL)||(pMax == NULL)||(DasSrcProp_ctrl(pMin) == NULL)||(DasSrcProp_ctrl(pMax) == NULL))
			continue;
		const DasSrcProp* pRes = DasSrcGroup_findProp(pG, "res");
		const DasSrcProp* pInt = DasSrcGroup_findProp(pG, "inter");
		const char* sStep = (pRes && DasSrcProp_ctrl(pRes)) ? "RES" :
		                    (pInt && DasSrcProp_ctrl(pInt)) ? "INTER" : NULL;
		fprintf(pOut, "  %s=MIN,MAX%s%s\n", DasSrcGroup_name(pG), sStep ? "," : "", sStep ? sStep : "");
	}
	prnPara(pOut, 2, "KEY alone sets a boolean key true, N alone starts from example N, and the "
	        "section prefix (coords., data., ...) may be dropped when unambiguous.  "
	        "See '" PROG " data -h'.");
}

static void describeSrc(const DasSrc* pSrc, FILE* pOut, const char* sUri)
{
	const char* sVer = DasSrc_version(pSrc);
	fprintf(pOut, "HttpStreamSrc%s%s:  %s\n", sVer[0] ? " " : "", sVer, DasSrc_name(pSrc));
	prnField(pOut, "Title:", DasSrc_title(pSrc));
	prnField(pOut, "Summary:", DasJdo_string(DasSrc_jdo(pSrc, D2FRAG_DESC)));
	prnField(pOut, "URI:", sUri);
	if(g_bVerbose){
		prnField(pOut, "From:", DasSrc_url(pSrc));
		size_t uBases = DasStreamSrc_urlCount(pSrc);
		if(uBases > 0){
			fprintf(pOut, "Server:   %s", DasStreamSrc_baseUrl(pSrc, 0));
			if(uBases > 1) fprintf(pOut, " (+%zu alternate)", uBases - 1);
			fputs("\n", pOut);
		}
	}
	prnAuth(pSrc, pOut);
	prnFormats(pSrc, pOut);

	if(g_uShow & SHOW_EXAMPLES) printExamples(pSrc, pOut);

	size_t uCtrls = 0;
	for(size_t u = 0; u < DasSrc_propCount(pSrc); ++u)
		if(DasSrcProp_ctrl(DasSrc_prop(pSrc, u)) != NULL) ++uCtrls;
	if(uCtrls == 0){
		fputs("\nThis source takes no query parameters.\n", pOut);
		if(!g_bVerbose) return;
	}
	else if(g_bVerbose){
		fputs("\nQuery keys, use as KEY=VALUE with 'data' or 'url':\n", pOut);
	}

	printKeyTables(pSrc, pOut);

	/* Usage hints are for the first dozen runs, not the hundredth.  They
	   live in 'data -h' and come back with -v. */
	if(g_bVerbose && (uCtrls > 0)) printShortcuts(pSrc, pOut);
}

/* ************************************************************************* */
/* Listing a catalog */

static void listCatalog(const DasNode* pNode, FILE* pOut, const char* sUri)
{
	const char* sType = jstr(DasNode_getJdo(pNode, NULL), D2FRAG_TYPE);
	const char* sVer = DasNode_version(pNode);
	fprintf(pOut, "%s%s%s:  %s\n", sType ? sType : "Node", sVer ? " " : "", sVer ? sVer : "",
	        DasNode_label(pNode) ? DasNode_label(pNode) : "");
	prnField(pOut, "Title:", DasNode_title(pNode));
	prnField(pOut, "URI:", sUri);
	if(g_bVerbose) prnField(pOut, "From:", DasNode_srcUrl(pNode));

	size_t uKids = DasNode_subCount(pNode);
	if(uKids == 0){ fputs("\nNo children listed.\n", pOut); return; }

	size_t wName = 4, wType = 4;
	for(size_t u = 0; u < uKids; ++u){
		const char* sName = DasNode_subName(pNode, u);
		const char* sT = jstr(DasNode_subStub(pNode, u), D2FRAG_TYPE);
		if(sName && (strlen(sName) > wName)) wName = strlen(sName);
		if(sT && (strlen(sT) > wType)) wType = strlen(sT);
	}
	int nDescCol = 2 + (int)wName + 2 + (int)wType + 3;

	fprintf(pOut, "\nChildren (%zu):\n\n", uKids);
	bool bUnsupported = false;
	for(size_t u = 0; u < uKids; ++u){
		const char* sName = DasNode_subName(pNode, u);
		const DasJdo* pStub = DasNode_subStub(pNode, u);
		const char* sT = jstr(pStub, D2FRAG_TYPE);
		const char* sDesc = jstr(pStub, D2FRAG_TITLE);
		if(sDesc == NULL) sDesc = jstr(pStub, D2FRAG_LABEL);
		if(sDesc == NULL) sDesc = jstr(pStub, D2FRAG_DESC);
		bool bKnown = (DasNode_subType(pNode, u) != d2node_inv);
		if(!bKnown) bUnsupported = true;
		fprintf(pOut, "  %-*s  %-*s%s  ", (int)wName, sName ? sName : "?",
		        (int)wType, sT ? sT : "?", bKnown ? " " : "*");
		fputsWrapped(sDesc ? sDesc : "", nDescCol, pOut);
		fputc('\n', pOut);
	}
	if(bUnsupported)
	{
		fputc('\n', pOut);
		prnPara(pOut, 2, "* this node type can be listed but not loaded or fetched by " PROG);
	}
}

/* What to show as a node's name.  A root has no URI: the caller's
 * fallback is shown when it has one, the location the node was loaded
 * from, else the node is called what it is.  Catalogs with a URI get
 * their child separator appended so the next step is obvious. */
static void nodeUri(
	const DasNode* pNode, const char* sFallback, char* sBuf, size_t uLen
){
	const char* sUri = DasNode_pathUri(pNode);
	if((sUri == NULL)||(sUri[0] == '\0')){
		if((sFallback != NULL) && (sFallback[0] != '\0')) snprintf(sBuf, uLen, "(the root, %s)", sFallback);
		else snprintf(sBuf, uLen, "(the root)");
		return;
	}
	const char* sSep = DasNode_isCatalog(pNode) ? DasNode_pathSep(pNode) : NULL;
	snprintf(sBuf, uLen, "%s%s", sUri, sSep ? sSep : "");
}

/* ************************************************************************* */
/* Catalog navigation
 *
 * DasCat (das3/catalog.h) owns the seed, the mounts, the credentials, the
 * agent and the memory of every node loaded, and resolves paths under one
 * policy.  What stays here is the command line spelling of that policy
 * and the wording of a failed lookup. */


/* A -r START or -m URL=URI value, or their environment stand-ins, turned
 * into catalog policy.  URL=URI names a node: the seed for -r, a mount for
 * -m.  For -r a URL alone is a seed with no name, a root, and anything
 * else is a URI prefix.  A mount always needs a name.
 *
 * The split is at the last '='.  What follows it is a name when it has a
 * scheme, "site:/x", or, since a mount under a root seed is named by a
 * path, when it is not a location and the URL before it carries no query
 * string.  A URL with a query string can end in "=x" itself, so a mount
 * on one needs a name with a scheme.  Command line mistakes are reported
 * here, the catalog logs its refusals.  False on either. */
static bool applyStartArg(DasCat* pCat, const char* sVal, bool bMount)
{
	const char* sOpt = bMount ? "-m" : "-r";
	const char* pEq = strrchr(sVal, '=');
	const char* sName = (pEq != NULL) ? pEq + 1 : NULL;
	bool bNamed = false;
	if((sName != NULL) && (sName[0] != '\0') && !das_isUrlOrFile(sName)){
		if(das_isUri(sName)) bNamed = true;
		else bNamed = (memchr(sVal, '?', (size_t)(pEq - sVal)) == NULL);
	}

	if(bNamed){
		char sUrl[URL_SZ];
		size_t uLen = (size_t)(pEq - sVal);
		if(uLen >= sizeof(sUrl)) uLen = sizeof(sUrl) - 1;
		memcpy(sUrl, sVal, uLen);
		sUrl[uLen] = '\0';
		if(!das_isUrlOrFile(sUrl)){
			fprintf(stderr, PROG ": %s takes URL=URI, '%s' before the '=' is not a URL or a file\n", sOpt, sUrl);
			return false;
		}
		if(bMount) return DasCat_mount(pCat, sUrl, sName) == DAS_OKAY;
		return DasCat_setSeed(pCat, sUrl, sName) == DAS_OKAY;
	}

	/* A location after the '=' is the old order, URI=URL, or "=URL" */
	if((sName != NULL) && das_isUrlOrFile(sName)){
		fprintf(stderr, PROG ": %s takes URL=URI, the location first and the catalog name "
		        "it stands for after the '='\n", sOpt);
		return false;
	}
	if(bMount){
		fprintf(stderr, PROG ": -m needs URL=URI, see -h\n");
		return false;
	}
	if(das_isUrlOrFile(sVal)) return DasCat_setSeed(pCat, sVal, NULL) == DAS_OKAY;   /* a root */
	if(pEq != NULL){
		fprintf(stderr, PROG ": -r takes URL=URI, and '%s' after the '=' is not a catalog name\n", sName);
		return false;
	}
	return DasCat_setPrefix(pCat, sVal) == DAS_OKAY;
}

/* Does a child's catalog entry provide any media type in the list?  The
   list is a set of exact types compared ignoring case, and the bare
   wildcard, star slash star, takes anything, including an entry with no
   provides list at all.  Parameters on the catalog side are cut. */
static bool stubProvides(const DasJdo* pStub, const char* sAccept)
{
	const DasJdo* pProv = (pStub == NULL) ? NULL : DasJdo_get(pStub, "provides");
	bool bList = (pProv != NULL) && (pProv->type == das_json_type_ary);

	const char* sWant = sAccept;
	while(true){
		sWant += strspn(sWant, ", \t");
		size_t uWant = strcspn(sWant, ", \t");
		if(uWant == 0) return false;
		if((uWant == 3) && (strncmp(sWant, "*/*", 3) == 0)) return true;
		if(bList){
			for(const das_json_ary_el* pEl = DasJdo_aryFirst(pProv); pEl; pEl = pEl->next){
				const char* sHas = DasJdo_string(pEl->value);
				if(sHas == NULL) continue;
				size_t uHas = strcspn(sHas, "; \t");
				if((uHas == uWant) && (strncasecmp(sWant, sHas, uWant) == 0)) return true;
			}
		}
		sWant += uWant;
	}
}

static const DasJdo* childStub(const DasNode* pNode, const char* sName)
{
	size_t uKids = DasNode_subCount(pNode);
	for(size_t u = 0; u < uKids; ++u)
		if(strcmp(DasNode_subName(pNode, u), sName) == 0) return DasNode_subStub(pNode, u);
	return NULL;
}

/* The library lists the children it can load; which one to fetch from
 * is this program's call, made on the media types each child provides.
 * Without -a it wants das3 streams and settles for anything, two asks.
 * An -a list is one ask and a set, so a user who lists two formats gets
 * every source that emits either and names one on the path if that is
 * several.  The chosen child is loaded by the session so credentials and
 * the cache follow. */
static DasSrc* pickSrc(DasCat* pCat, DasNode* pNode, const char* sAccept)
{
	if(DasNode_isStreamSrc(pNode)) return new_DasSrc_node(pNode);

	const char* aNames[MAX_KIDS];
	size_t uKids = dassrc_loadable(pNode, aNames, MAX_KIDS);
	if(uKids == 0){
		fprintf(stderr, PROG ": nothing under %s is a source this program can fetch from\n",
		        DasNode_pathUri(pNode));
		return NULL;
	}
	if(uKids > MAX_KIDS) uKids = MAX_KIDS;

	const char* aAsk[2] = { DEF_ACCEPT, "*/*" };
	size_t uAsks = 2;
	if(sAccept != NULL){ aAsk[0] = sAccept; uAsks = 1; }

	const char* aPick[MAX_KIDS];
	size_t uPick = 0;
	const char* sAsk = NULL;
	for(size_t a = 0; (a < uAsks) && (uPick == 0); ++a){
		sAsk = aAsk[a];
		for(size_t k = 0; k < uKids; ++k)
			if(stubProvides(childStub(pNode, aNames[k]), sAsk)) aPick[uPick++] = aNames[k];
	}

	if(uPick != 1){
		if(uPick == 0)
			fprintf(stderr, PROG ": nothing under %s provides %s, the sources it has are:", DasNode_pathUri(pNode), sAsk);
		else
			fprintf(stderr, PROG ": %s offers %zu sources for %s, put one on the end of the path:", DasNode_pathUri(pNode), uPick, sAsk);
		const char** aShow = (uPick == 0) ? aNames : aPick;
		size_t uShow = (uPick == 0) ? uKids : uPick;
		for(size_t u = 0; u < uShow; ++u) fprintf(stderr, " %s", aShow[u]);
		fputc('\n', stderr);
		return NULL;
	}

	DasNode* pSub = DasCat_subNode(pCat, pNode, aPick[0]);
	if(pSub == NULL){
		fprintf(stderr, PROG ": couldn't load source '%s' under %s\n", aPick[0], DasNode_pathUri(pNode));
		return NULL;
	}
	return new_DasSrc_node(pSub);
}

/* A path did not resolve.  Say whether the policy or the catalog is to
 * blame, and for the catalog, which child was the problem and what its
 * parent offers.  Call right after the failed DasCat_resolve(). */
static void explainUnresolved(DasCat* pCat, const char* sPath)
{
	char sFullUri[URI_SZ];
	strcpyz(sFullUri, DasCat_lastUri(pCat), sizeof(sFullUri));

	/* The common mistake with a seed is asking for something it does not
	   cover, say so instead of hunting for a parent */
	if(!DasCat_covers(pCat, sFullUri)){
		daslog_error_v("'%s' is outside the -r seed %s, use -m to mount it on the "
		               "federated catalog instead", sFullUri, DasCat_prefix(pCat));
		return;
	}

	const char* sRest = NULL;
	DasNode* pNode = DasCat_nearest(pCat, &sRest);
	if((pNode == NULL)||(sRest == NULL)||(sRest[0] == '\0')){
		/* A root seed has no URI, so the path to it is empty; name the file */
		if((sFullUri[0] == '\0') && DasCat_hasSeed(pCat))
			daslog_error_v("Couldn't load the seed %s", DasCat_seedUrl(pCat));
		else
			daslog_error_v("Couldn't resolve catalog path '%s'", sFullUri);
		return;
	}
	const char* sParentUri = DasCat_lastUri(pCat);

	/* Only the first component below the deepest node that loaded can be
	   the culprit */
	char sChild[NAME_SZ];
	strcpyz(sChild, sRest, sizeof(sChild));
	char* pSlash = strchr(sChild, '/');
	if(pSlash != NULL) *pSlash = '\0';

	char sMsg[ERR_SZ];
	size_t uKids = DasNode_subCount(pNode);
	size_t n = 0;
	bool bFound = false;
	for(size_t u = 0; u < uKids; ++u){
		const char* sName = DasNode_subName(pNode, u);
		if(strieq(sName, sChild)){
			const char* sType = jstr(DasNode_subStub(pNode, u), D2FRAG_TYPE);
			if(DasNode_subType(pNode, u) == d2node_inv)
				n = snprintf(sMsg, sizeof(sMsg), "'%s' under %s is a %s node, which " PROG
				             " can list but not load or fetch from", sChild, sParentUri, sType ? sType : "?");
			else
				n = snprintf(sMsg, sizeof(sMsg), "'%s' under %s is listed but could not be loaded",
				             sChild, sParentUri);
			bFound = true;
			break;
		}
	}
	if(!bFound) n = snprintf(sMsg, sizeof(sMsg), "No child named '%s' under %s", sChild, sParentUri);
	if(uKids > 0){
		n += snprintf(sMsg + n, sizeof(sMsg) - n, ".  Children:");
		for(size_t u = 0; (u < uKids) && (n < sizeof(sMsg)); ++u)
			n += snprintf(sMsg + n, sizeof(sMsg) - n, " %s", DasNode_subName(pNode, u));
	}
	daslog_error_v("%s", sMsg);
}

/* Everything main() owns.  The catalog frees its nodes, then the credential
 * manager they borrow.  Returns its argument so exits read as one line. */
static int finish(DasCat* pCat, int nRet)
{
	del_DasCat(pCat);
	return nRet;
}

/* ************************************************************************* */
/* Fetching */

static int streamUrl(DasCat* pCat, const char* sUrl, float rTimeout)
{
	DasHttpResp res;
	memset(&res, 0, sizeof(DasHttpResp));

	daslog_info_v("Requesting %s", sUrl);

	if(!das_http_getBody(sUrl, DasCat_agent(pCat), DasCat_credMngr(pCat), &res, rTimeout)){
		int nRet = RET_SERVER;
		if((res.nCode == 401)||(res.nCode == 403)) nRet = RET_AUTH;
		else if(res.nCode == 400) nRet = RET_QUERY;
		daslog_error_v("%s", res.sError ? res.sError : "Request failed");
		if(res.nCode == 400)
			daslog_error("The server rejected the query.  Check the keys with '"
			               PROG " node' or try the URL from '" PROG " url' in a browser");
		DasHttpResp_freeFields(&res);
		return nRet;
	}

	char sFinal[URL_SZ];
	das_url_toStr(&(res.url), sFinal, sizeof(sFinal));
	if(strcmp(sFinal, sUrl) != 0) daslog_info_v("Redirected to %s", sFinal);
	if(res.pMime) daslog_info_v("Content type %s", res.pMime);

	das_stdio_binary(stdout);

	/* The library closes the connection, success or not.  Only the write
	 * failure is ours to classify, everything else is the server's fault
	 * as far as the exit code is concerned. */
	int64_t nTotal = 0;
	int nRet = RET_OK;
	switch(das_http_copyBody(&res, stdout, &nTotal, NULL, NULL)){
	case DAS_OKAY:  nRet = RET_OK;     break;
	case DASERR_IO: nRet = RET_WRITE;  break;
	default:        nRet = RET_SERVER; break;
	}
	fflush(stdout);

	DasHttpResp_freeFields(&res);

	daslog_info_v("%" PRId64 " bytes written", nTotal);
	return nRet;
}

/* ************************************************************************* */

/* Columns available on stdout, 0 when it is not a terminal */
static int termWidth(void)
{
	if(!isatty(fileno(stdout))) return 0;
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO info;
	if(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
		return info.srWindow.Right - info.srWindow.Left + 1;
#else
	struct winsize ws;
	if((ioctl(fileno(stdout), TIOCGWINSZ, &ws) == 0) && (ws.ws_col > 0)) return ws.ws_col;
#endif
	const char* sCols = getenv("COLUMNS");
	if(isDigits(sCols)) return atoi(sCols);
	return 80;
}

/* -s SECTIONS: comma separated section names to a bit set */
static bool parseShow(const char* sList, unsigned* puShow)
{
	char sCopy[256];
	strcpyz(sCopy, sList, sizeof(sCopy));
	*puShow = 0;
	for(char* pTok = strtok(sCopy, ","); pTok != NULL; pTok = strtok(NULL, ",")){
		das_src_sec_e sec;
		if(strieq(pTok, "all")) *puShow = SHOW_ALL;
		else if(strieq(pTok, "examples")||strieq(pTok, "example")||strieq(pTok, "ex")) *puShow |= SHOW_EXAMPLES;
		else if(secAlias(pTok, &sec)) *puShow |= (1u << sec);
		else return false;
	}
	return true;
}

int main(int argc, char** argv)
{
	das_init(PROG, DASERR_DIS_EXIT, 0, DASLOG_WARN, NULL);
	daslog_set_showline(DASLOG_NOTHING);   /* messages here are for users, not debuggers */

	char sAgent[64];
	snprintf(sAgent, sizeof(sAgent), PROG " (das2C %s)", das_version());
	DasCat* pCat = NULL;   /* made once the arguments are in, see below */

	const char* sCmd = NULL;
	const char* sPath = NULL;
	const char* sCacheArg = NULL;
	const char* sStartArg = NULL;
	const char* aMountArg[MAX_MOUNTS];
	size_t uMountArg = 0;
	const char* sAccept = NULL;   /* -a, else the two step default in pickSrc */
	bool bHelp = false;
	bool bFresh = false;
	bool bQuiet = false;
	const char* aKeyVals[MAX_KEYS];   /* KEY=VALUE arguments, applied in order */
	size_t uKeyVals = 0;
	const char* aRaw[MAX_RAW];        /* -p PARAM=VALUE arguments */
	size_t uRaw = 0;
	int nExample = DASSRC_BLANK_REQ;
	float rTimeout = 10.0;

	for(int i = 1; i < argc; ++i){
		const char* sArg = argv[i];
		if((strcmp(sArg, "-h") == 0)||(strcmp(sArg, "--help") == 0)){ bHelp = true; continue; }
		if((strcmp(sArg, "-V") == 0)||(strcmp(sArg, "--version") == 0)){
			printf(PROG " (das2C %s)\n", das_version());
			return RET_OK;
		}
		if(strcmp(sArg, "-q") == 0){ daslog_setlevel(DASLOG_ERROR); bQuiet = true; continue; }
		if(strcmp(sArg, "-v") == 0){ daslog_setlevel(DASLOG_INFO); g_bVerbose = true; g_uShow = SHOW_ALL; continue; }
		if(strcmp(sArg, "-f") == 0){ bFresh = true; continue; }
		if(strcmp(sArg, "--start") == 0) sArg = "-r";
		if(strcmp(sArg, "--mount") == 0) sArg = "-m";
		if((strcmp(sArg, "-r") == 0)||(strcmp(sArg, "-m") == 0)||(strcmp(sArg, "-s") == 0)||
		   (strcmp(sArg, "-C") == 0)||(strcmp(sArg, "-p") == 0)||(strcmp(sArg, "-t") == 0)||
		   (strcmp(sArg, "-l") == 0)||(strcmp(sArg, "-a") == 0)){
			if(i + 1 >= argc){
				fprintf(stderr, PROG ": option %s needs a value, use -h for help\n", sArg);
				return RET_USAGE;
			}
			const char* sVal = argv[++i];
			switch(sArg[1]){
			case 'r': sStartArg = sVal; break;
			case 'm':
				if(uMountArg >= MAX_MOUNTS){
					fprintf(stderr, PROG ": at most %d mounts\n", MAX_MOUNTS);
					return RET_USAGE;
				}
				aMountArg[uMountArg++] = sVal;
				break;
			case 'a': sAccept = sVal; break;
			case 's':
				if(!parseShow(sVal, &g_uShow)){
					fprintf(stderr, PROG ": -s takes a list from coords, data, formats, options, "
					        "examples, all\n");
					return RET_USAGE;
				}
				break;
			case 'C': sCacheArg = sVal; break;
			case 't': rTimeout = (float)atof(sVal); break;
			case 'l':{
				int nLvl = daslog_strlevel(sVal);
				daslog_setlevel(nLvl);
				break;
			}
			case 'p':{
				const char* pEq = strchr(sVal, '=');
				if((pEq == NULL)||(pEq == sVal)||(uRaw >= MAX_RAW)){
					fprintf(stderr, PROG ": -p needs PARAM=VALUE (at most %d of them)\n", MAX_RAW);
					return RET_USAGE;
				}
				aRaw[uRaw++] = sVal;
				break;
			}
			}
			continue;
		}
		if(sArg[0] == '-'){
			fprintf(stderr, PROG ": unknown option %s, use -h for help\n", sArg);
			return RET_USAGE;
		}

		/* Positionals: command, path, then KEY=VALUE pairs.  A bare number is
		   an example index, not a path. */
		if(sCmd == NULL){ sCmd = sArg; continue; }
		if((sPath == NULL) && (strchr(sArg, '=') == NULL) && !isDigits(sArg)){ sPath = sArg; continue; }
		if(uKeyVals < MAX_KEYS) aKeyVals[uKeyVals++] = sArg;
	}
	if(bHelp){ prnHelp(sCmd); return RET_OK; }
	g_nCols = termWidth();

	/* Credentials: saved ones are tried before the terminal prompt.  The
	   catalog owns the manager and hands it to every node and request. */
	char sCredFile[256] = {'\0'};
	snprintf(sCredFile, sizeof(sCredFile), "%s" DAS_DSEPS CRED_FILE, das_userdir());
	DasCredMngr* pMgr = new_CredMngr(sCredFile);

	/* CredMngr_load logs an error for a missing file even though its
	   contract says a missing file is not an error.  Most users have no
	   saved credentials, so look first. */
	FILE* pTest = fopen(sCredFile, "r");
	if(pTest != NULL){
		fclose(pTest);
		CredMngr_load(pMgr, NULL, NULL);
	}
	pCat = new_DasCat(pMgr, sAgent);

	/* Environment fallbacks for -r and -m, for people who live in one test
	   catalog.  The start is applied first so a mount can complain about a
	   conflicting one. */
	const char* sEnv = getenv(START_ENV);
	if((sStartArg == NULL) && (sEnv != NULL) && (sEnv[0] != '\0')) sStartArg = sEnv;
	sEnv = getenv(MOUNT_ENV);
	if((uMountArg == 0) && (sEnv != NULL) && (sEnv[0] != '\0')) aMountArg[uMountArg++] = sEnv;
	if((sStartArg != NULL) && !applyStartArg(pCat, sStartArg, false))
		return finish(pCat, RET_USAGE);
	for(size_t u = 0; u < uMountArg; ++u)
		if(!applyStartArg(pCat, aMountArg[u], true)) return finish(pCat, RET_USAGE);

	if(sCmd == NULL){
		fprintf(stderr, PROG ": no command given, use -h for help\n");
		return finish(pCat, RET_USAGE);
	}

	if(strcmp(sCmd, "seeds") == 0){
		size_t uSeeds = 0;
		const char** psSeeds = das_seed_urls(&uSeeds);
		for(size_t u = 0; u < uSeeds; ++u) printf("%s\n", psSeeds[u]);
		return finish(pCat, RET_OK);
	}

	bool bNode = (strcmp(sCmd, "node") == 0)||(strcmp(sCmd, "ls") == 0);
	bool bJson = (strcmp(sCmd, "json") == 0);
	bool bUrl  = (strcmp(sCmd, "url") == 0);
	bool bData = (strcmp(sCmd, "data") == 0)||(strcmp(sCmd, "get") == 0);
	if(!bNode && !bJson && !bUrl && !bData){
		fprintf(stderr, PROG ": unknown command '%s', use -h for help\n", sCmd);
		return finish(pCat, RET_USAGE);
	}
	/* Mounts are consulted on the way, they are not a place to start */
	if((bJson || bUrl || bData) && (sPath == NULL) && !DasCat_hasSeed(pCat)){
		fprintf(stderr, PROG ": the %s command needs a PATH, use -h for help\n", sCmd);
		return finish(pCat, RET_USAGE);
	}
	if(bNode && (uKeyVals > 0))
		daslog_warn("KEY=VALUE arguments are ignored by the node command");

	/* Node cache: the session's default unless told otherwise.  -f refreshes
	   rather than disables so that the run after a catalog change leaves the
	   cache current. */
	if((sCacheArg != NULL) && strieq(sCacheArg, "none"))
		DasCat_setCache(pCat, NULL, 0);
	else if(sCacheArg != NULL)
		DasCat_setCache(pCat, sCacheArg, bFresh ? 0 : DASCAT_DEF_CACHE_SEC);
	else if(bFresh)
		DasCat_setCache(pCat, DasCat_cacheDir(pCat), 0);

	/* A fragment on the path is only meaningful for the json command */
	char sPathBuf[1024] = {'\0'};
	const char* sFrag = NULL;
	if(sPath != NULL){
		strcpyz(sPathBuf, sPath, sizeof(sPathBuf));
		char* pHash = strchr(sPathBuf, '#');
		if(pHash != NULL){ *pHash = '\0'; sFrag = pHash + 1; }
		/* "a/b/" means the same as "a/b", shell completion adds the slash */
		if(!das_isUrlOrFile(sPathBuf)){
			size_t u = strlen(sPathBuf);
			while((u > 1) && (sPathBuf[u-1] == '/') && (sPathBuf[u-2] != ':')) sPathBuf[--u] = '\0';
		}
		sPath = sPathBuf;
	}

	DasNode* pNode = DasCat_resolve(pCat, sPath);
	if(pNode == NULL){
		explainUnresolved(pCat, sPath);
		return finish(pCat, RET_NODE);
	}
	char sShow[1200];
	nodeUri(pNode, DasCat_lastUri(pCat), sShow, sizeof(sShow));

	int nRet = RET_OK;

	if(bJson){
		const DasJdo* pJdo = DasNode_getJdo(pNode, sFrag);
		if(pJdo == NULL){
			daslog_error_v("Node has no element at '%s'", sFrag ? sFrag : "");
			nRet = RET_NODE;
		}
		else{
			char* sPretty = (char*)DasJdo_writePretty(pJdo, "  ", "\n", NULL);
			if(sPretty){ fputs(sPretty, stdout); fputs("\n", stdout); free(sPretty); }
		}
		return finish(pCat, nRet);
	}

	if(bNode){
		if(DasNode_isCatalog(pNode)){
			/* A source container is one step from the goal.  Users never type
			   the source's name, so by default act as if they had asked for the
			   default source and describe it under the container's URI.  The
			   container listing itself only matters once more than one source
			   type can be fetched from, so it is verbose only for now. */
			das_node_type_e nType = DasNode_type(pNode);
			bool bSrcSet = (nType == d2node_srcset)||(nType == d2node_collection);
			if(!bSrcSet || g_bVerbose) listCatalog(pNode, stdout, sShow);

			if(bSrcSet){
				DasSrc* pSrc = pickSrc(pCat, pNode, sAccept);
				if(pSrc == NULL){
					/* pickSrc said why; the listing shows what to name */
					if(!g_bVerbose) listCatalog(pNode, stdout, sShow);
					nRet = RET_NODE;
				}
				else if(!g_bVerbose){
					describeSrc(pSrc, stdout, sShow);
				}
				else{
					/* The child's name is the tail of its path URI, after the
					   separator this container uses */
					const char* sSrcUri = DasSrc_path(pSrc);
					const char* sSep = DasNode_pathSep(pNode);
					const char* sName = sSrcUri;
					if(sSep && sSep[0]){
						const char* p = strstr(sSrcUri, sSep);
						while(p){ sName = p + strlen(sSep); p = strstr(sName, sSep); }
					}
					char sNote[256];
					snprintf(sNote, sizeof(sNote), "Default source: %s", sName);
					fputc('\n', stdout);
					prnPara(stdout, 0, sNote);
					fputc('\n', stdout);
					describeSrc(pSrc, stdout, sSrcUri);
				}
				del_DasSrc(pSrc);
			}
		}
		else if(DasNode_isStreamSrc(pNode)){
			DasSrc* pSrc = new_DasSrc_node(pNode);
			if(pSrc == NULL){
				nRet = RET_NODE;
			}
			else{
				describeSrc(pSrc, stdout, sShow);
				del_DasSrc(pSrc);
			}
		}
		else{
			const char* sType = jstr(DasNode_getJdo(pNode, NULL), D2FRAG_TYPE);
			printf("%s:  %s\n%-10s%s\n", sType ? sType : "Node",
			       DasNode_label(pNode) ? DasNode_label(pNode) : "", "URI:", sShow);
			if(g_bVerbose) printf("From:     %s\n", DasNode_srcUrl(pNode));
			printf("\n" PROG " has no detailed printer for this node type, try the json command.\n");
		}
		return finish(pCat, nRet);
	}

	/* url or data.  The source outlives nothing here, but it does not need
	   the tree either, so the order of teardown does not matter. */
	DasSrc* pSrc = pickSrc(pCat, pNode, sAccept);
	if(pSrc == NULL) return finish(pCat, RET_NODE);

	/* Example selectors among the keys: examples.N, or bare N as the shortcut,
	   the same pairing as data.sprt and sprt.  They are pulled out first since
	   an example is the base the other keys modify. */
	size_t uKept = 0;
	for(size_t u = 0; u < uKeyVals; ++u){
		const char* sArg = aKeyVals[u];
		const char* sNum = sArg;
		if(strchr(sArg, '=') == NULL){
			const char* pDot = strchr(sArg, '.');
			if(pDot != NULL){
				size_t uLen = (size_t)(pDot - sArg);
				bool bSec = ((uLen == 7) && (strncmp(sArg, "example", 7) == 0)) ||
				            ((uLen == 8) && (strncmp(sArg, "examples", 8) == 0));
				if(bSec){
					if(!isDigits(pDot + 1)){
						daslog_error_v("Examples are numbered, there is no '%s'", sArg);
						del_DasSrc(pSrc);
						return finish(pCat, RET_QUERY);
					}
					sNum = pDot + 1;
				}
			}
			if(isDigits(sNum)){ nExample = atoi(sNum); continue; }
		}
		aKeyVals[uKept++] = sArg;
	}
	uKeyVals = uKept;

	/* Nothing asked for at all: run the first example so there is always a
	   quick way to see a source work, and say so */
	if((nExample < 0) && (uKeyVals == 0) && (uRaw == 0) && (DasSrc_exampleCount(pSrc) > 0)){
		nExample = 0;
		if(!bQuiet)
			fprintf(stderr, PROG ": no query given, using example 0, \"%s\"\n", DasSrc_exampleLabel(pSrc, 0));
	}

	/* Order matters: the example is the base, explicit keys override it, and
	   -p raw parameters have the last word when the URL is built. */
	DasReq* pReq = DasSrc_newReq(pSrc, nExample);
	if(pReq == NULL){
		del_DasSrc(pSrc);
		return finish(pCat, RET_QUERY);
	}
	for(size_t u = 0; u < uKeyVals; ++u){
		char sKey[KEY_SZ];
		const char* pEq = strchr(aKeyVals[u], '=');
		const char* sVal = "true";    /* bare KEY toggles a boolean on */
		if(pEq != NULL){
			size_t uLen = (size_t)(pEq - aKeyVals[u]);
			if(uLen >= KEY_SZ) uLen = KEY_SZ - 1;
			memcpy(sKey, aKeyVals[u], uLen);
			sKey[uLen] = '\0';
			sVal = pEq + 1;
		}
		else{
			strcpyz(sKey, aKeyVals[u], KEY_SZ);
		}
		DasReq_set(pReq, sKey, sVal);   /* only an empty key fails, and none is */
	}
	for(size_t u = 0; u < uRaw; ++u){
		char sName[KEY_SZ];
		const char* pEq = strchr(aRaw[u], '=');   /* present, main checked */
		size_t uLen = (size_t)(pEq - aRaw[u]);
		if(uLen >= KEY_SZ) uLen = KEY_SZ - 1;
		memcpy(sName, aRaw[u], uLen);
		sName[uLen] = '\0';
		DasReq_setRaw(pReq, sName, pEq + 1);
	}

	/* The library logs the first bad entry, a missing required parameter,
	   or a source with nothing to query, so a failed URL needs no more
	   words here.  Too small a buffer is the one case that is ours. */
	char sUrl[URL_SZ];
	if(bUrl){
		size_t uNeed = DasStreamSrc_url(pSrc, pReq, 0, sUrl, sizeof(sUrl));
		if(uNeed == 0) nRet = RET_QUERY;
		else if(uNeed >= sizeof(sUrl)){ daslog_error("Query URL too long"); nRet = RET_QUERY; }
		else printf("%s\n", sUrl);
		del_DasReq(pReq);
		del_DasSrc(pSrc);
		return finish(pCat, nRet);
	}

	/* data: try each base URL in turn until one answers.  A server that
	   answers with an error is an answer, only connection failures move on.
	   Base 0 is always asked for, so a source with no base URLs at all is
	   reported by the library like any other unusable one. */
	size_t uBases = DasStreamSrc_urlCount(pSrc);
	for(size_t u = 0; ; ++u){
		size_t uNeed = DasStreamSrc_url(pSrc, pReq, u, sUrl, sizeof(sUrl));
		if(uNeed == 0){ nRet = RET_QUERY; break; }
		if(uNeed >= sizeof(sUrl)){ daslog_error("Query URL too long"); nRet = RET_QUERY; break; }
		nRet = streamUrl(pCat, sUrl, rTimeout);
		if((nRet != RET_SERVER)||(u + 1 >= uBases)) break;
		daslog_warn_v("Trying alternate server %s", DasStreamSrc_baseUrl(pSrc, u + 1));
	}

	del_DasReq(pReq);
	del_DasSrc(pSrc);
	finish(pCat, nRet);
	das_finish();
	return nRet;
}
