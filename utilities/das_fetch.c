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
 * The program has two halves.  The small half is catalog navigation, which
 * is the library's job (DasCat in das3/catalog.h, nodes in das3/node.h); only
 * the command line spelling of the root policy lives here.  The large half is
 * turning a data source's "interface" section into command line keys, and
 * turning the user's KEY=VALUE arguments back into HTTP GET parameters via
 * the source's "protocol" section.  The translation rules are those of
 * the das catalog schema for HttpStreamSrc nodes, so any client following
 * the schema agrees on what a source definition means.
 *
 * Two generations of HttpStreamSrc node are in the wild (0.6 from 2018,
 * 0.7 from dasflex) and they spell things differently.  Layout detection
 * here is structural (does a group have a "props" sub-dictionary) rather
 * than by version string, and the 0.6 spellings are handled inline where
 * they differ.  Once the 0.6 nodes are retired those branches can go.
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
#include <das3/http.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#include <sys/ioctl.h>
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

#define MAX_SETTABLES 512
#define MAX_PARAMS     96
#define MAX_RAW        32
/* Identifiers in a node (property, variable and group names) follow the das3
 * rule enforced by das_assert_valid_id(): at most 63 characters.  A full key
 * is "section.group.prop", so size it from that rule, not from a guess. */
#define NAME_SZ  DAS_MAX_ID_BUFSZ
#define KEY_SZ   (8 + 2*DAS_MAX_ID_BUFSZ)
#define VAL_SZ  512
#define URI_SZ 1024
#define URL_SZ 4096
#define ERR_SZ 1024

#define ROOT_ENV  "DASFETCH_ROOT"
#define MOUNT_ENV "DASFETCH_MOUNT"

/* ************************************************************************* */
/* Help */

static void prnHelpMain(void)
{
	fputs(
"NAME\n"
"   " PROG " - fetch data by das catalog path, no server URLs required\n"
"\n"
"USAGE\n"
"   " PROG " [options] roots\n"
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
"   roots            Print the compiled in catalog root URLs.\n"
"   node [PATH]      List a catalog's children, or a data source's examples\n"
"                    and query keys.  No PATH shows the default root.\n"
"   json PATH        Print a node's raw JSON, or a fragment of it.\n"
"   url  PATH ...    Print the HTTP GET URL a query turns into.\n"
"   data PATH ...    Run the query, write the response body to stdout.\n"
"\n"
"   Each command has its own help: '" PROG " data -h' explains query keys,\n"
"   '" PROG " node -h' explains the listings.\n"
"\n"
"PATHS\n"
"   Paths are catalog paths, not web paths.  Any of these forms work:\n"
"\n"
"      uiowa/juno/wav/survey             relative to the root prefix, by\n"
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
"   -r, --root ROOT    Where paths start, in one of two forms.\n"
"                      A catalog URI prefix for relative paths, default\n"
"                      '" DASCAT_DEF_PREFIX "':\n"
"                         -r tag:das2.org,2012:test:/\n"
"                      URI=URL, the node at URL declared to be the catalog\n"
"                      node URI.  It becomes the only root: paths are\n"
"                      relative to it unless they are full URIs under it,\n"
"                      and nothing outside it can be reached.  Any -m mounts\n"
"                      join it to make an offline catalog:\n"
"                         -r site:=./my_site_copy.json\n"
"                         -r 10.1037/a0040251=https://doi.org/10.1037/a0040251/root.json\n"
"                      A node has no name of its own, names are composed\n"
"                      walking down from the root, so the URI is required.\n"
"                      Without -r the environment variable " ROOT_ENV " is\n"
"                      used the same way, if set.\n"
"   -m, --mount URI=URL\n"
"                      Mount a node on the federated catalog: the node at URL\n"
"                      stands in for URI and everything under it, the rest\n"
"                      of the catalog is unchanged.  Relative paths start at\n"
"                      the mounted node, or at the -r prefix if one is given:\n"
"                         -m site:/uiowa/tracers/tracers-1=\\\n"
"                            https://tracers-dev.physics.uiowa.edu/stream/source/tracers-1.json\n"
"                      May be repeated.  The environment variable " MOUNT_ENV "\n"
"                      stands in for -m, if set.\n"
"\n"
"   -s SECTIONS        For 'node', which parts of a source to print, a comma\n"
"                      separated list from: examples, coords, data, formats,\n"
"                      options, all.  The default leaves out formats.\n"
"   -p PARAM=VALUE     Send a raw HTTP GET parameter, bypassing the interface\n"
"                      translation.  Repeatable.\n"
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
"   Show what is at a catalog path.  With no PATH, the default root, which\n"
"   lists the federated sites.\n"
"\n"
"CATALOGS\n"
"   A catalog node prints its own name and URI, then one line per child:\n"
"   name, node type, and title.  Append a child's name to the path to step\n"
"   down.  The URI line ends with the separator this catalog uses, so the\n"
"   next path is URI plus child name.  A '*' after the type marks node\n"
"   types " PROG " can list but not load or fetch from (Das2DSDF, WebSocSrc).\n"
"\n"
"   A SourceSet or Collection holds several descriptions of one dataset.\n"
"   Only the one 'data' and 'url' would use is shown: the child named\n"
"   'flex', else the first HttpStreamSrc.  -v lists them all.  To use\n"
"   another, add its name to the path, as in uiowa/juno/wav/survey/das2.\n"
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
"   " PROG " -r site:/uiowa/tracers=https://tracers-dev.physics.uiowa.edu/stream/root.json node\n"
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
"      " PROG " -r site:/uiowa/tracers=https://tracers-dev.physics.uiowa.edu/stream/root.json \\\n"
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

static void prnHelpRoots(void)
{
	fputs(
"USAGE\n"
"   " PROG " roots\n"
"\n"
"   Print the compiled in URLs of the federated catalog root, one per line.\n"
"   The root is mirrored, the first URL that answers is used.  A -r ROOT\n"
"   argument or the " ROOT_ENV " variable replaces it, a -m URI=URL argument\n"
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
	else if(strcmp(sCmd, "roots") == 0) prnHelpRoots();
	else prnHelpMain();
}

/* ************************************************************************* */
/* Small string and JSON helpers */

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

static bool parseBool(const char* s, bool* pOut)
{
	if(s == NULL) return false;
	if(strieq(s,"true")||strieq(s,"yes")||strieq(s,"on")||strieq(s,"1")||
	   strieq(s,"t")||strieq(s,"y")){ *pOut = true; return true; }
	if(strieq(s,"false")||strieq(s,"no")||strieq(s,"off")||strieq(s,"0")||
	   strieq(s,"f")||strieq(s,"n")){ *pOut = false; return true; }
	return false;
}

/* NULL tolerant wrappers.  DasJdo_get() is a slash path lookup, so the dot
 * in HTTP parameter names such as "read.time.min" is not special to it. */
static const DasJdo* jget(const DasJdo* p, const char* sPath)
{
	return (p == NULL) ? NULL : DasJdo_get(p, sPath);
}

static const char* jstr(const DasJdo* p, const char* sPath)
{
	return DasJdo_string(jget(p, sPath));
}

static bool jisdict(const DasJdo* p){ return (p != NULL)&&(p->type == das_json_type_dict); }
static bool jisary(const DasJdo* p){  return (p != NULL)&&(p->type == das_json_type_ary); }
static bool jtrue(const DasJdo* p){   return (p != NULL)&&(p->type == das_json_type_true); }
static bool jisbool(const DasJdo* p){
	return (p != NULL)&&((p->type == das_json_type_true)||(p->type == das_json_type_false));
}

/* Scalar JSON value to text.  False for null, missing, dicts and arrays.
 * Example settings and defaults can be strings, numbers or booleans, and the
 * value sent to the server is text in any case. */
static bool jscalar(const DasJdo* p, char* sBuf, size_t uLen)
{
	if((p == NULL)||(uLen == 0)) return false;
	switch(p->type){
	case das_json_type_str:
		strcpyz(sBuf, ((const das_json_str*)p->value)->string, uLen);
		return true;
	case das_json_type_num:{
		const das_json_num* pNum = (const das_json_num*)p->value;
		size_t u = (pNum->number_size < uLen - 1) ? pNum->number_size : uLen - 1;
		memcpy(sBuf, pNum->number, u);
		sBuf[u] = '\0';
		return true;
	}
	case das_json_type_true:  strcpyz(sBuf, "true", uLen);  return true;
	case das_json_type_false: strcpyz(sBuf, "false", uLen); return true;
	default: return false;
	}
}

/* Percent encode everything but RFC 3986 unreserved characters.  Servers
 * decode before use, so over-encoding ':' in time strings is harmless, and
 * it is what most URL encoders do anyway. */
static void urlEncode(const char* sIn, char* sOut, size_t uOut)
{
	static const char* sHex = "0123456789ABCDEF";
	size_t u = 0;
	for(const unsigned char* p = (const unsigned char*)sIn; *p && (u + 4 < uOut); ++p){
		if(isalnum(*p)||(*p == '-')||(*p == '.')||(*p == '_')||(*p == '~')){
			sOut[u++] = (char)*p;
		}
		else{
			sOut[u++] = '%';
			sOut[u++] = sHex[*p >> 4];
			sOut[u++] = sHex[*p & 0x0F];
		}
	}
	sOut[u] = '\0';
}

/* ************************************************************************* */
/* The settable model: a flat list of interface properties that have a 'set'
 * block, with enough context to print them and to translate values.
 *
 * DasHttpSrc, DasSettable and DasHttpParam are named for where they are
 * expected to end up, libdas3 next to DasNode, once the 1.0 catalog schema
 * settles what they read (see cf_notes/api_enlargement.md).  The _print and
 * _describe functions are das_fetch presentation and would stay behind. */

typedef enum { SEC_COORD = 0, SEC_DATA, SEC_FORMAT, SEC_OPTION, SEC_COUNT } sec_e;

/* Section names as the 0.7 catalogs spell them.  Keys are built from these,
   input accepts the singular forms too, see secAlias() */
static const char* g_sSecName[SEC_COUNT] = {"coords", "data", "formats", "options"};

typedef struct das_settable {
	sec_e sec;
	char sGroup[NAME_SZ];     /* "time", "lshell", "das"; empty for options */
	char sProp[NAME_SZ];      /* "min", "enabled", "serial" */
	char sKey[KEY_SZ];        /* "coord.time.min" */
	const DasJdo* pGroup;     /* the coordinate, variable, or format dict */
	const DasJdo* pProp;      /* the property dict: value, set, type, label... */
	const DasJdo* pSet;       /* the set dict: param, flag, pval, enum... */
	const char* sParam;       /* set/param, the HTTP GET parameter name */
	const DasJdo* pHParam;    /* protocol httpParams entry for sParam */
	bool bRequired;           /* set/required or httpParams/required */
	bool bHasDefault;         /* value is not null */
	char sDefault[VAL_SZ];
	bool bUserSet;
	char sValue[VAL_SZ];
} DasSettable;

typedef struct das_http_param {
	char sName[NAME_SZ];
	char sVal[VAL_SZ];
} DasHttpParam;

typedef struct das_http_src {
	DasNode* pNode;
	const DasJdo* pIface;
	const DasJdo* pProto;
	const DasJdo* pHttpParams;
	const DasJdo* pBaseUrls;
	bool bLegacy;             /* v0.6 layout */
	DasSettable aSet[MAX_SETTABLES];
	size_t uSet;
	DasHttpParam aOut[MAX_PARAMS];
	size_t uOut;
} DasHttpSrc;

/* Property name aliases.  The 0.6 nodes spell out minimum/maximum/resolution
 * and interval, 0.7 nodes abbreviate.  Users may type either. */
static const char* canonProp(const char* s)
{
	if(strieq(s, "minimum")||strieq(s, "min"))        return "min";
	if(strieq(s, "maximum")||strieq(s, "max"))        return "max";
	if(strieq(s, "resolution")||strieq(s, "res"))     return "res";
	if(strieq(s, "interval")||strieq(s, "inter"))     return "inter";
	if(strieq(s, "enable")||strieq(s, "enabled"))     return "enabled";
	return s;
}

static bool propEq(const char* a, const char* b)
{
	return strieq(canonProp(a), canonProp(b));
}

static bool secAlias(const char* s, sec_e* pSec)
{
	if(strieq(s,"coord")||strieq(s,"coords")||strieq(s,"coordinate")||strieq(s,"coordinates")){
		*pSec = SEC_COORD; return true;
	}
	if(strieq(s,"data")){ *pSec = SEC_DATA; return true; }
	if(strieq(s,"format")||strieq(s,"formats")||strieq(s,"fmt")){ *pSec = SEC_FORMAT; return true; }
	if(strieq(s,"option")||strieq(s,"options")||strieq(s,"opt")||strieq(s,"opts")){
		*pSec = SEC_OPTION; return true;
	}
	return false;
}

/* ************************************************************************* */
/* Display settings and terminal wrapping */

/* Which parts of a source 'node' prints: one bit per section plus examples */
#define SHOW_EXAMPLES (1u << SEC_COUNT)
#define SHOW_ALL      ((1u << (SEC_COUNT + 1)) - 1)
static unsigned g_uShow = SHOW_ALL & ~(1u << SEC_FORMAT);
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
	if(sText == NULL) return;
	fprintf(pOut, "%-10s", sLabel);
	fputsWrapped(sText, 10, pOut);
	fputc('\n', pOut);
}

/* Type of a settable for validation and display.  Preference order: the
 * property's own type, an enum in set, a boolean default, a flag toggle,
 * the protocol parameter's type, then string. */
static const char* DasSettable_type(const DasSettable* p)
{
	const char* sType = jstr(p->pProp, "type");
	if(sType != NULL){
		if(strieq(sType, "bool")) return "boolean";
		return sType;
	}
	if(jisary(jget(p->pSet, "enum"))) return "enum";
	if(jisbool(jget(p->pProp, "value"))) return "boolean";
	if(jstr(p->pSet, "flag") != NULL) return "boolean";
	sType = jstr(p->pHParam, "type");
	if(sType != NULL){
		if(strieq(sType, "FlagSet")||strieq(sType, "flag_set")) return "string";
		return sType;
	}
	return "string";
}

static bool DasSettable_isBool(const DasSettable* p)
{
	return strieq(DasSettable_type(p), "boolean");
}

/* Units for display.  0.7 puts a units string on the property; both versions
 * may carry a units dict with a "value" at the group level (0.7 under props,
 * 0.6 directly). */
static const char* DasSettable_units(const DasSettable* p)
{
	const DasJdo* pU = jget(p->pProp, "units");
	if(pU != NULL){
		if(pU->type == das_json_type_str) return DasJdo_string(pU);
		if(jstr(pU, "value")) return jstr(pU, "value");
	}
	if(p->sec == SEC_COORD){
		const char* s = jstr(p->pGroup, "props/units/value");
		if(s == NULL) s = jstr(p->pGroup, "units/value");
		if(s != NULL) return s;
	}
	if(strieq(DasSettable_type(p), "isotime")) return "UTC";
	return "";
}

static const char* groupName(const DasSettable* p)
{
	const char* s = jstr(p->pGroup, "label");
	if(s == NULL) s = jstr(p->pGroup, "name");
	if(s == NULL) s = jstr(p->pGroup, "title");
	return s ? s : p->sGroup;
}

/* Description column.  The property's own title or label wins.  Without
 * one, coordinate properties have conventional meanings worth spelling out,
 * and an enable toggle is best described by the thing it enables. */
static const char* DasSettable_desc(const DasSettable* p, char* sBuf, size_t uLen)
{
	const char* s = jstr(p->pProp, "title");
	if(s == NULL) s = jstr(p->pProp, "summary");
	if(s == NULL) s = jstr(p->pProp, "label");
	if(s == NULL) s = jstr(p->pProp, "name");
	if(s != NULL) return s;

	const char* sProp = canonProp(p->sProp);
	if(strcmp(sProp, "enabled") == 0){
		if(p->sec == SEC_FORMAT){
			snprintf(sBuf, uLen, "Output as %s", groupName(p));
		}
		else{
			const char* sTitle = jstr(p->pGroup, "title");
			if(sTitle == NULL) sTitle = groupName(p);
			snprintf(sBuf, uLen, "Output %s", sTitle);
		}
		return sBuf;
	}
	if(p->sec == SEC_COORD){
		if(strcmp(sProp, "min") == 0){ 
			snprintf(sBuf, uLen, "Minimum %s value to output", groupName(p)); 
			return sBuf; 
		}
		if(strcmp(sProp, "max") == 0){ 
			snprintf(sBuf, uLen, "Maximum %s value to output", groupName(p)); 
			return sBuf; 
		}
		if(strcmp(sProp, "res") == 0){ 
			snprintf(sBuf, uLen, "Average over %s bins of this width", groupName(p)); 
			return sBuf; 
		}
		if(strcmp(sProp, "inter") == 0){ 
			snprintf(sBuf, uLen, "Step between output %s values", groupName(p));
			return sBuf; 
		}
	}
	if(p->sec != SEC_OPTION){
		snprintf(sBuf, uLen, "%s %s", groupName(p), p->sProp);
		return sBuf;
	}
	return "";
}

/* Allowed values for display: enum entries, a range, or a coordinate's
 * valid range for min/max properties. */
static void DasSettable_values(const DasSettable* p, char* sBuf, size_t uLen)
{
	sBuf[0] = '\0';
	const DasJdo* pEnum = jget(p->pSet, "enum");
	if(jisary(pEnum)){
		size_t u = 0;
		char sVal[VAL_SZ];
		for(const das_json_ary_el* pEl = DasJdo_aryFirst(pEnum); pEl; pEl = pEl->next){
			if(!jscalar(jget(pEl->value, "value"), sVal, sizeof(sVal))) continue;
			if(u + strlen(sVal) + 3 >= uLen){ strcpyz(sBuf + u, "...", uLen - u); return; }
			u += snprintf(sBuf + u, uLen - u, "%s%s", (u > 0) ? ", " : "", sVal);
		}
		return;
	}
	const DasJdo* pRange = jget(p->pSet, "range");
	if(!jisary(pRange)) pRange = jget(p->pHParam, "range");
	if(!jisary(pRange) && (p->sec == SEC_COORD) &&
	   (propEq(p->sProp, "min") || propEq(p->sProp, "max")))
		pRange = jget(p->pGroup, "validRange");
	if(jisary(pRange)){
		const das_json_ary_el* pEl = DasJdo_aryFirst(pRange);
		char sLo[VAL_SZ] = {'\0'}, sHi[VAL_SZ] = {'\0'};
		if(pEl) jscalar(pEl->value, sLo, sizeof(sLo));
		if(pEl && pEl->next) jscalar(pEl->next->value, sHi, sizeof(sHi));
		snprintf(sBuf, uLen, "%s to %s", sLo, sHi);
		return;
	}
	if(DasSettable_isBool(p)){
		strcpyz(sBuf, "true, false", uLen);
	}
}

/* ************************************************************************* */
/* Flattening the interface */

static void DasHttpSrc_addProp(
	DasHttpSrc* pS, sec_e sec, const char* sGroup, const DasJdo* pGroup,
	const char* sProp, const DasJdo* pProp
){
	if(!jisdict(pProp)) return;
	const DasJdo* pSet = jget(pProp, "set");
	if(!jisdict(pSet)) return;       /* documentation only, not settable */

	if(pS->uSet >= MAX_SETTABLES){
		daslog_warn_v("Source lists more than %d settable properties, ignoring %s",
		              MAX_SETTABLES, sProp);
		return;
	}
	DasSettable* p = pS->aSet + pS->uSet;
	memset(p, 0, sizeof(DasSettable));
	pS->uSet += 1;

	p->sec = sec;
	strcpyz(p->sGroup, sGroup, NAME_SZ);
	strcpyz(p->sProp, sProp, NAME_SZ);
	if(sec == SEC_OPTION)
		snprintf(p->sKey, KEY_SZ, "%s.%s", g_sSecName[sec], sProp);
	else
		snprintf(p->sKey, KEY_SZ, "%s.%s.%s", g_sSecName[sec], sGroup, sProp);

	p->pGroup = pGroup;
	p->pProp = pProp;
	p->pSet = pSet;
	p->sParam = jstr(pSet, "param");
	p->pHParam = p->sParam ? jget(pS->pHttpParams, p->sParam) : NULL;
	p->bRequired = jtrue(jget(pSet, "required")) || jtrue(jget(p->pHParam, "required"));
	p->bHasDefault = jscalar(jget(pProp, "value"), p->sDefault, VAL_SZ);
}

/* A group is a coordinate, a data variable, a format, or the whole options
 * block.  0.7 groups keep properties under "props", 0.6 groups hold them
 * directly beside label/title/units.  Anything without a 'set' is skipped
 * either way, so iterating the group itself is safe for 0.6. */
static void DasHttpSrc_addGroup(DasHttpSrc* pS, sec_e sec, const char* sGroup, const DasJdo* pGroup)
{
	if(!jisdict(pGroup)) return;
	const DasJdo* pProps = jget(pGroup, "props");
	const DasJdo* pIter = jisdict(pProps) ? pProps : pGroup;
	for(const das_json_dict_el* pEl = DasJdo_dictFirst(pIter); pEl; pEl = pEl->next)
		DasHttpSrc_addProp(pS, sec, sGroup, pGroup, pEl->name->string, pEl->value);
}

static void DasHttpSrc_addSection(DasHttpSrc* pS, sec_e sec, const DasJdo* pSec)
{
	if(!jisdict(pSec)) return;
	for(const das_json_dict_el* pEl = DasJdo_dictFirst(pSec); pEl; pEl = pEl->next)
		DasHttpSrc_addGroup(pS, sec, pEl->name->string, pEl->value);
}

static bool DasHttpSrc_init(DasHttpSrc* pS, DasNode* pNode, char* sErr, size_t uErr)
{
	memset(pS, 0, sizeof(DasHttpSrc));
	pS->pNode = pNode;
	const DasJdo* pDom = DasNode_getJdo(pNode, NULL);
	pS->pIface = jget(pDom, "interface");
	pS->pProto = jget(pDom, "protocol");
	if(!jisdict(pS->pProto)){
		snprintf(sErr, uErr, "Source node %s has no 'protocol' section", DasNode_srcUrl(pNode));
		return false;
	}
	pS->pHttpParams = jget(pS->pProto, "httpParams");
	if(pS->pHttpParams == NULL) pS->pHttpParams = jget(pS->pProto, "http_params");
	pS->pBaseUrls = jget(pS->pProto, "baseUrls");
	if(pS->pBaseUrls == NULL) pS->pBaseUrls = jget(pS->pProto, "base_urls");
	if(!jisary(pS->pBaseUrls)){
		snprintf(sErr, uErr, "Source node %s has no base URL list", DasNode_srcUrl(pNode));
		return false;
	}
	pS->bLegacy = (jget(pS->pProto, "http_params") != NULL) ||
	              (jget(pS->pIface, "coordinates") != NULL);

	const DasJdo* pSec = jget(pS->pIface, "coords");
	if(pSec == NULL) pSec = jget(pS->pIface, "coordinates");
	DasHttpSrc_addSection(pS, SEC_COORD,  pSec);
	DasHttpSrc_addSection(pS, SEC_DATA,   jget(pS->pIface, "data"));
	DasHttpSrc_addSection(pS, SEC_FORMAT, jget(pS->pIface, "formats"));
	DasHttpSrc_addGroup(pS, SEC_OPTION, "", jget(pS->pIface, "options"));
	return true;
}

/* ************************************************************************* */
/* Key lookup */

/* Split "a.b.c" into parts, returns the count or 0 if malformed */
static int splitKey(const char* sKey, char aParts[3][NAME_SZ])
{
	int n = 0;
	const char* pBeg = sKey;
	while(true){
		const char* pEnd = strchr(pBeg, '.');
		size_t uLen = pEnd ? (size_t)(pEnd - pBeg) : strlen(pBeg);
		if((uLen == 0)||(uLen >= NAME_SZ)||(n == 3)) return 0;
		memcpy(aParts[n], pBeg, uLen);
		aParts[n][uLen] = '\0';
		++n;
		if(pEnd == NULL) break;
		pBeg = pEnd + 1;
	}
	return n;
}

/* Find settables matching a dotted key.  Returns the match count and fills
 * ppOut with up to nMax of them.  More than one match is an error the caller
 * reports with the candidate list; zero means no such key. */
static int DasHttpSrc_match(DasHttpSrc* pS, const char* sKey, DasSettable** ppOut, int nMax)
{
	char aParts[3][NAME_SZ];
	int nParts = splitKey(sKey, aParts);
	if(nParts == 0) return 0;

	int n = 0;
	sec_e sec;
	DasSettable* p = NULL;

#define _ADD_MATCH if(n < nMax) ppOut[n] = p; ++n;

	if(nParts == 3){
		if(!secAlias(aParts[0], &sec)) return 0;
		for(size_t u = 0; u < pS->uSet; ++u){
			p = pS->aSet + u;
			if((p->sec == sec) && strieq(p->sGroup, aParts[1]) && propEq(p->sProp, aParts[2])){
				_ADD_MATCH
			}
		}
		return n;
	}

	if(nParts == 2){
		/* "option.X" is a section qualified option, "SEC.GROUP" toggles a
		   variable, anything else is GROUP.PROP in any section */
		if(secAlias(aParts[0], &sec)){
			for(size_t u = 0; u < pS->uSet; ++u){
				p = pS->aSet + u;
				if(p->sec != sec) continue;
				if((sec == SEC_OPTION) && propEq(p->sProp, aParts[1])){ _ADD_MATCH }
				else if((sec != SEC_OPTION) && strieq(p->sGroup, aParts[1]) &&
				        propEq(p->sProp, "enabled")){ _ADD_MATCH }
			}
			if(n > 0) return n;
		}
		for(size_t u = 0; u < pS->uSet; ++u){
			p = pS->aSet + u;
			if(strieq(p->sGroup, aParts[0]) && propEq(p->sProp, aParts[1])){ _ADD_MATCH }
		}
		return n;
	}

	/* One part: an option, or a variable/format toggle */
	for(size_t u = 0; u < pS->uSet; ++u){
		p = pS->aSet + u;
		if((p->sec == SEC_OPTION) && propEq(p->sProp, aParts[0])){ _ADD_MATCH }
		else if((p->sec != SEC_OPTION) && strieq(p->sGroup, aParts[0]) &&
		        propEq(p->sProp, "enabled")){ _ADD_MATCH }
	}
	if(n > 0) return n;

	/* Last resort, a bare property name.  Unique in practice for things
	   like 'serial', ambiguous for 'fracSecs' shared by das and csv, and
	   the ambiguity message then lists the full keys. */
	for(size_t u = 0; u < pS->uSet; ++u){
		p = pS->aSet + u;
		if((p->sec != SEC_OPTION) && propEq(p->sProp, aParts[0])){ _ADD_MATCH }
	}
#undef _ADD_MATCH
	return n;
}

/* If sKey (with or without a coord. prefix) names a coordinate group that
 * has settable properties, return the group name as spelled by the node */
static const char* DasHttpSrc_coordGroup(DasHttpSrc* pS, const char* sKey)
{
	char aParts[3][NAME_SZ];
	int nParts = splitKey(sKey, aParts);
	const char* sGroup = NULL;
	sec_e sec;
	if(nParts == 1) sGroup = aParts[0];
	else if((nParts == 2) && secAlias(aParts[0], &sec) && (sec == SEC_COORD)) sGroup = aParts[1];
	else return NULL;

	for(size_t u = 0; u < pS->uSet; ++u){
		if((pS->aSet[u].sec == SEC_COORD) && strieq(pS->aSet[u].sGroup, sGroup))
			return pS->aSet[u].sGroup;
	}
	return NULL;
}

static bool DasHttpSrc_hasProp(const DasHttpSrc* pS, sec_e sec, const char* sGroup, const char* sProp)
{
	for(size_t u = 0; u < pS->uSet; ++u){
		const DasSettable* p = pS->aSet + u;
		if((p->sec == sec) && strieq(p->sGroup, sGroup) && propEq(p->sProp, sProp))
			return true;
	}
	return false;
}

static DasSettable* DasHttpSrc_find(DasHttpSrc* pS, sec_e sec, const char* sGroup, const char* sProp)
{
	for(size_t u = 0; u < pS->uSet; ++u){
		DasSettable* p = pS->aSet + u;
		if((p->sec == sec) && strieq(p->sGroup, sGroup) && propEq(p->sProp, sProp))
			return p;
	}
	return NULL;
}

/* Validate and store one user value. */
static bool DasSettable_set(DasSettable* p, const char* sVal, char* sErr, size_t uErr)
{
	const char* sType = DasSettable_type(p);

	if(strieq(sType, "boolean")){
		bool b;
		if(!parseBool(sVal, &b)){
			snprintf(sErr, uErr, "'%s' is not a boolean value for %s, use true or false",
			         sVal, p->sKey);
			return false;
		}
		strcpyz(p->sValue, b ? "true" : "false", VAL_SZ);
		p->bUserSet = true;
		return true;
	}

	const DasJdo* pEnum = jget(p->pSet, "enum");
	if(jisary(pEnum)){
		char sItem[VAL_SZ];
		for(const das_json_ary_el* pEl = DasJdo_aryFirst(pEnum); pEl; pEl = pEl->next){
			if(jscalar(jget(pEl->value, "value"), sItem, sizeof(sItem)) && strieq(sItem, sVal)){
				strcpyz(p->sValue, sItem, VAL_SZ);   /* node's spelling */
				p->bUserSet = true;
				return true;
			}
		}
		char sVals[ERR_SZ/2];
		DasSettable_values(p, sVals, sizeof(sVals));
		snprintf(sErr, uErr, "'%s' is not an allowed value for %s, choose one of: %s",
		         sVal, p->sKey, sVals);
		return false;
	}

	strcpyz(p->sValue, sVal, VAL_SZ);
	p->bUserSet = true;
	return true;
}

/* Apply one KEY=VALUE from the command line, including the shortcuts. */
static bool DasHttpSrc_set(DasHttpSrc* pS, const char* sKey, const char* sVal, char* sErr, size_t uErr)
{
	/* Coordinate range shortcut: GROUP=MIN,MAX[,STEP] */
	const char* sGroup = DasHttpSrc_coordGroup(pS, sKey);
	if(sGroup != NULL){
		const char* aProps[3] = {"min", "max", NULL};
		aProps[2] = DasHttpSrc_hasProp(pS, SEC_COORD, sGroup, "res") ? "res" : "inter";

		char sCopy[VAL_SZ];
		strcpyz(sCopy, sVal, VAL_SZ);
		char* pTok = sCopy;
		for(int i = 0; i < 4; ++i){
			char* pComma = strchr(pTok, ',');
			if(pComma) *pComma = '\0';
			if(i == 3){
				snprintf(sErr, uErr, "Too many values for coordinate %s, at most MIN,MAX,STEP", sGroup);
				return false;
			}
			if(pTok[0] != '\0'){
				DasSettable* p = DasHttpSrc_find(pS, SEC_COORD, sGroup, aProps[i]);
				if(p == NULL){
					snprintf(sErr, uErr, "Coordinate %s has no settable '%s' property", sGroup, aProps[i]);
					return false;
				}
				if(!DasSettable_set(p, pTok, sErr, uErr)) return false;
			}
			if(pComma == NULL) break;
			pTok = pComma + 1;
		}
		return true;
	}

	DasSettable* aMatch[8];
	int n = DasHttpSrc_match(pS, sKey, aMatch, 8);
	if(n == 0){
		snprintf(sErr, uErr, "No query key named '%s' for this source, run '" PROG
		         " node PATH' to list them", sKey);
		return false;
	}
	if(n > 1){
		size_t u = snprintf(sErr, uErr, "Key '%s' is ambiguous, use one of:", sKey);
		for(int i = 0; (i < n) && (i < 8) && (u < uErr); ++i)
			u += snprintf(sErr + u, uErr - u, " %s", aMatch[i]->sKey);
		return false;
	}
	return DasSettable_set(aMatch[0], sVal, sErr, uErr);
}

/* ************************************************************************* */
/* Translation to protocol parameters */

static DasHttpParam* DasHttpSrc_outFind(DasHttpSrc* pS, const char* sName)
{
	for(size_t u = 0; u < pS->uOut; ++u)
		if(strcmp(pS->aOut[u].sName, sName) == 0) return pS->aOut + u;
	return NULL;
}

static bool DasHttpSrc_outSet(DasHttpSrc* pS, const char* sName, const char* sVal, char* sErr, size_t uErr)
{
	DasHttpParam* p = DasHttpSrc_outFind(pS, sName);
	if(p == NULL){
		if(pS->uOut >= MAX_PARAMS){
			snprintf(sErr, uErr, "More than %d HTTP parameters in one query", MAX_PARAMS);
			return false;
		}
		p = pS->aOut + pS->uOut;
		pS->uOut += 1;
		strcpyz(p->sName, sName, NAME_SZ);
		p->sVal[0] = '\0';
	}
	strcpyz(p->sVal, sVal, VAL_SZ);
	return true;
}

/* Flag set parameters accumulate: several interface toggles map to one
 * parameter whose value is the flags joined by a separator */
static bool DasHttpSrc_outAddFlag(
	DasHttpSrc* pS, const char* sName, const char* sFlagVal, const char* sSep,
	char* sErr, size_t uErr
){
	DasHttpParam* p = DasHttpSrc_outFind(pS, sName);
	if((p == NULL)||(p->sVal[0] == '\0'))
		return DasHttpSrc_outSet(pS, sName, sFlagVal, sErr, uErr);
	if(strlen(p->sVal) + strlen(sSep) + strlen(sFlagVal) >= VAL_SZ){
		snprintf(sErr, uErr, "Value for HTTP parameter %s is too long", sName);
		return false;
	}
	strcat(p->sVal, sSep);
	strcat(p->sVal, sFlagVal);
	return true;
}

static bool DasHttpSrc_translate(DasHttpSrc* pS, DasSettable* p, const char* sVal, char* sErr, size_t uErr)
{
	if(p->sParam == NULL){
		snprintf(sErr, uErr, "Key %s has no 'param' in its set block, the node is broken", p->sKey);
		return false;
	}

	char sFlag[NAME_SZ] = {'\0'};
	char sPVal[VAL_SZ] = {'\0'};
	bool bHavePVal = false;
	strcpyz(sFlag, jstr(p->pSet, "flag"), NAME_SZ);
	bHavePVal = jscalar(jget(p->pSet, "pval"), sPVal, VAL_SZ);

	const DasJdo* pEnum = jget(p->pSet, "enum");
	if(jisary(pEnum)){
		/* DasSettable_set already checked membership, now pick up per-item
		   overrides of the protocol value or flag */
		char sItem[VAL_SZ];
		for(const das_json_ary_el* pEl = DasJdo_aryFirst(pEnum); pEl; pEl = pEl->next){
			if(!jscalar(jget(pEl->value, "value"), sItem, sizeof(sItem))) continue;
			if(!strieq(sItem, sVal)) continue;
			if(jscalar(jget(pEl->value, "pval"), sPVal, VAL_SZ)) bHavePVal = true;
			if(jstr(pEl->value, "flag")) strcpyz(sFlag, jstr(pEl->value, "flag"), NAME_SZ);
			break;
		}
	}
	else if(DasSettable_isBool(p)){
		bool bUser = false;
		parseBool(sVal, &bUser);
		const DasJdo* pTrig = jget(p->pSet, "value");
		if(jisbool(pTrig)){
			/* The set block fires for one state only, usually true.  The other
			   state is the default and has nothing to send. */
			if(bUser != jtrue(pTrig)) return true;
		}
		if(!bHavePVal && (sFlag[0] == '\0')){
			strcpyz(sPVal, bUser ? "true" : "false", VAL_SZ);
			bHavePVal = true;
		}
	}

	if(sFlag[0] != '\0'){
		char sPath[NAME_SZ + 8];
		snprintf(sPath, sizeof(sPath), "flags/%s", sFlag);
		const DasJdo* pF = jget(p->pHParam, sPath);
		if(pF == NULL){
			snprintf(sErr, uErr, "Flag '%s' for key %s is not defined under protocol "
			         "parameter %s, the node is broken", sFlag, p->sKey, p->sParam);
			return false;
		}
		char sFVal[VAL_SZ];
		if(!jscalar(jget(pF, "value"), sFVal, VAL_SZ)) strcpyz(sFVal, sFlag, VAL_SZ);
		const char* sPrefix = jstr(pF, "prefix");
		if(sPrefix != NULL){
			char sTmp[VAL_SZ];
			snprintf(sTmp, VAL_SZ, "%s%s", sPrefix, sFVal);
			strcpyz(sFVal, sTmp, VAL_SZ);
		}
		const char* sSep = jstr(p->pHParam, "flagSep");
		if(sSep == NULL) sSep = jstr(p->pHParam, "flag_sep");
		if(sSep == NULL) sSep = " ";
		return DasHttpSrc_outAddFlag(pS, p->sParam, sFVal, sSep, sErr, uErr);
	}

	return DasHttpSrc_outSet(pS, p->sParam, bHavePVal ? sPVal : sVal, sErr, uErr);
}

/* Turn everything the user set (plus required defaults) into parameters.
 * Adds to pS->aOut, overriding same named entries, so a 0.6 example's raw
 * protocol parameters can be loaded first and still lose to the user's
 * keys. */
static bool DasHttpSrc_build(DasHttpSrc* pS, char* sErr, size_t uErr)
{
	for(size_t u = 0; u < pS->uSet; ++u){
		DasSettable* p = pS->aSet + u;
		const char* sVal = NULL;

		if(p->bUserSet) sVal = p->sValue;
		else if(p->bRequired && p->bHasDefault) sVal = p->sDefault;
		else continue;

		/* A required default fills a hole, it never replaces a value that an
		   example or -p already supplied */
		if(!p->bUserSet && p->sParam && DasHttpSrc_outFind(pS, p->sParam)) continue;

		/* A value equal to the default carries no information unless the
		   server insists on hearing it */
		if(p->bUserSet && !p->bRequired && p->bHasDefault){
			bool bSame = false;
			if(DasSettable_isBool(p)){
				bool bA = false, bB = false;
				bSame = parseBool(sVal, &bA) && parseBool(p->sDefault, &bB) && (bA == bB);
			}
			else{
				bSame = strieq(sVal, p->sDefault);
			}
			if(bSame) continue;
		}
		if(!DasHttpSrc_translate(pS, p, sVal, sErr, uErr)) return false;
	}
	return true;
}

/* Every protocol parameter marked required must be present.  Servers
 * enforce this, and their reply is a 400 whose body we never see, so say it
 * here in terms of the interface key the user can set. */
static bool DasHttpSrc_checkRequired(DasHttpSrc* pS, char* sErr, size_t uErr)
{
	if(!jisdict(pS->pHttpParams)) return true;
	for(const das_json_dict_el* pEl = DasJdo_dictFirst(pS->pHttpParams); pEl; pEl = pEl->next){
		if(!jtrue(jget(pEl->value, "required"))) continue;
		const char* sName = pEl->name->string;
		if(DasHttpSrc_outFind(pS, sName) != NULL) continue;

		size_t u = snprintf(sErr, uErr, "Required parameter missing, set it with");
		int nKeys = 0;
		for(size_t i = 0; (i < pS->uSet) && (u < uErr); ++i){
			if(pS->aSet[i].sParam && (strcmp(pS->aSet[i].sParam, sName) == 0)){
				u += snprintf(sErr + u, uErr - u, "%s %s=VALUE", nKeys ? " or" : "", pS->aSet[i].sKey);
				++nKeys;
			}
		}
		if(nKeys == 0)
			snprintf(sErr + u, uErr - u, " -p %s=VALUE (no interface key sets it)", sName);
		else
			snprintf(sErr + u, uErr - u, " (HTTP parameter %s)", sName);
		return false;
	}
	return true;
}

static size_t DasHttpSrc_baseCount(const DasHttpSrc* pS)
{
	return jisary(pS->pBaseUrls) ? ((const das_json_ary*)pS->pBaseUrls->value)->length : 0;
}

static const char* DasHttpSrc_baseUrl(const DasHttpSrc* pS, size_t uIdx)
{
	const das_json_ary_el* pEl = DasJdo_aryFirst(pS->pBaseUrls);
	for(size_t u = 0; (u < uIdx) && pEl; ++u) pEl = pEl->next;
	return pEl ? DasJdo_string(pEl->value) : NULL;
}

/* Base URL plus the parameters.  0.6 base URLs already carry a query string
 * (server=dataset&dataset=...), 0.7 ones do not, hence the joiner check. */
static bool DasHttpSrc_url(const DasHttpSrc* pS, size_t uBase, char* sUrl, size_t uLen)
{
	const char* sBase = DasHttpSrc_baseUrl(pS, uBase);
	if(sBase == NULL) return false;
	size_t u = snprintf(sUrl, uLen, "%s", sBase);
	const char* sJoin = strchr(sBase, '?') ? "&" : "?";
	char sEnc[VAL_SZ * 3];
	for(size_t i = 0; (i < pS->uOut) && (u < uLen); ++i){
		urlEncode(pS->aOut[i].sVal, sEnc, sizeof(sEnc));
		u += snprintf(sUrl + u, uLen - u, "%s%s=%s", sJoin, pS->aOut[i].sName, sEnc);
		sJoin = "&";
	}
	return u < uLen;
}

/* ************************************************************************* */
/* Examples */

/* 0.7 example settings are keyed by interface path, "coords/time/props/min".
 * Convert to the dotted key form: drop "props", singular section name. */
static bool exPathToKey(const char* sPath, char* sKey, size_t uLen)
{
	char sCopy[KEY_SZ * 2];
	strcpyz(sCopy, sPath, sizeof(sCopy));
	size_t u = 0;
	int nTok = 0;
	sec_e sec;
	for(char* pTok = strtok(sCopy, "/"); pTok; pTok = strtok(NULL, "/")){
		if(strcmp(pTok, "props") == 0) continue;
		const char* sOut = pTok;
		if((nTok == 0) && secAlias(pTok, &sec)) sOut = g_sSecName[sec];
		if(u + strlen(sOut) + 2 >= uLen) return false;
		u += snprintf(sKey + u, uLen - u, "%s%s", nTok ? "." : "", sOut);
		++nTok;
	}
	return nTok > 0;
}

static size_t DasHttpSrc_exampleCount(const DasHttpSrc* pS)
{
	if(pS->bLegacy){
		const DasJdo* pEx = jget(pS->pProto, "examples");
		return jisdict(pEx) ? ((const das_json_dict*)pEx->value)->length : 0;
	}
	const DasJdo* pEx = jget(pS->pIface, "examples");
	return jisary(pEx) ? ((const das_json_ary*)pEx->value)->length : 0;
}

/* The example's own JSON object, or NULL if there is no such example */
static const DasJdo* DasHttpSrc_example(const DasHttpSrc* pS, size_t uIdx, const char** psName)
{
	if(psName) *psName = NULL;
	if(pS->bLegacy){
		const das_json_dict_el* pEl = DasJdo_dictFirst(jget(pS->pProto, "examples"));
		for(size_t u = 0; (u < uIdx) && pEl; ++u) pEl = pEl->next;
		if(pEl == NULL) return NULL;
		if(psName) *psName = pEl->name->string;
		return pEl->value;
	}
	const das_json_ary_el* pEl = DasJdo_aryFirst(jget(pS->pIface, "examples"));
	for(size_t u = 0; (u < uIdx) && pEl; ++u) pEl = pEl->next;
	return pEl ? pEl->value : NULL;
}

static const char* DasHttpSrc_exampleLabel(const DasHttpSrc* pS, size_t uIdx)
{
	const char* sName = NULL;
	const DasJdo* pEx = DasHttpSrc_example(pS, uIdx, &sName);
	if(pEx == NULL) return "";
	const char* sLabel = jstr(pEx, "label");
	if(sLabel == NULL) sLabel = jstr(pEx, "title");
	if(sLabel == NULL) sLabel = sName;
	return sLabel ? sLabel : "";
}

/* Print the arguments that reproduce an example, indented under its row */
static void DasHttpSrc_printExampleArgs(
	const DasHttpSrc* pS, size_t uIdx, int nCol, FILE* pOut
){
	char sKey[KEY_SZ], sVal[VAL_SZ];
	char sArgs[4096] = {'\0'};
	size_t u = 0;
	const DasJdo* pEx = DasHttpSrc_example(pS, uIdx, NULL);
	if(pEx == NULL) return;

	for(int i = 0; i < nCol; ++i) fputc(' ', pOut);
	if(pS->bLegacy){
		u += snprintf(sArgs + u, sizeof(sArgs) - u, "(protocol parameters) ");
		const DasJdo* pParams = jget(pEx, "http_params");
		for(const das_json_dict_el* pP = DasJdo_dictFirst(pParams); pP && (u < sizeof(sArgs)); pP = pP->next){
			if(jscalar(pP->value, sVal, sizeof(sVal)))
				u += snprintf(sArgs + u, sizeof(sArgs) - u, "%s=%s ", pP->name->string, sVal);
		}
	}
	else{
		const DasJdo* pSettings = jget(pEx, "settings");
		for(const das_json_dict_el* pP = DasJdo_dictFirst(pSettings); pP && (u < sizeof(sArgs)); pP = pP->next){
			if(exPathToKey(pP->name->string, sKey, sizeof(sKey)) && jscalar(pP->value, sVal, sizeof(sVal)))
				u += snprintf(sArgs + u, sizeof(sArgs) - u, "%s=%s ", sKey, sVal);
		}
	}
	fputsWrapped(sArgs, nCol, pOut);
	fputs("\n", pOut);
}

/* The examples table: number and what it provides, arguments only with -v */
static void DasHttpSrc_printExamples(const DasHttpSrc* pS, FILE* pOut)
{
	size_t uEx = DasHttpSrc_exampleCount(pS);
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
		fputsWrapped(DasHttpSrc_exampleLabel(pS, u), nCol, pOut);
		fputc('\n', pOut);
		if(g_bVerbose) DasHttpSrc_printExampleArgs(pS, u, nCol, pOut);
	}
}

/* Load example uIdx.  0.7 examples set interface keys and go through the
 * normal translation.  0.6 examples are protocol parameters and are applied
 * raw after translation, see main(). */
static bool DasHttpSrc_applyExample(
	DasHttpSrc* pS, size_t uIdx, DasHttpParam* aRaw, size_t* puRaw, size_t uMaxRaw,
	char* sErr, size_t uErr
){
	if(uIdx >= DasHttpSrc_exampleCount(pS)){
		snprintf(sErr, uErr, "Source has %zu example(s), there is no example %zu",
		         DasHttpSrc_exampleCount(pS), uIdx);
		return false;
	}
	char sKey[KEY_SZ], sVal[VAL_SZ];
	if(pS->bLegacy){
		const das_json_dict_el* pEl = DasJdo_dictFirst(jget(pS->pProto, "examples"));
		for(size_t u = 0; u < uIdx; ++u) pEl = pEl->next;
		const DasJdo* pParams = jget(pEl->value, "http_params");
		for(const das_json_dict_el* pP = DasJdo_dictFirst(pParams); pP; pP = pP->next){
			if(!jscalar(pP->value, sVal, sizeof(sVal))) continue;
			if(*puRaw >= uMaxRaw){
				snprintf(sErr, uErr, "Example %zu has more than %zu parameters", uIdx, uMaxRaw);
				return false;
			}
			strcpyz(aRaw[*puRaw].sName, pP->name->string, NAME_SZ);
			strcpyz(aRaw[*puRaw].sVal, sVal, VAL_SZ);
			*puRaw += 1;
		}
		return true;
	}
	const das_json_ary_el* pEl = DasJdo_aryFirst(jget(pS->pIface, "examples"));
	for(size_t u = 0; u < uIdx; ++u) pEl = pEl->next;
	const DasJdo* pSettings = jget(pEl->value, "settings");
	for(const das_json_dict_el* pP = DasJdo_dictFirst(pSettings); pP; pP = pP->next){
		if(!exPathToKey(pP->name->string, sKey, sizeof(sKey))){
			snprintf(sErr, uErr, "Can't interpret example setting path '%s'", pP->name->string);
			return false;
		}
		if(!jscalar(pP->value, sVal, sizeof(sVal))) continue;
		if(!DasHttpSrc_set(pS, sKey, sVal, sErr, uErr)) return false;
	}
	return true;
}

/* ************************************************************************* */
/* Stream version and format selection */

/* ************************************************************************* */
/* Describing a source */

static void prnAuth(const DasHttpSrc* pS, FILE* pOut)
{
	const DasJdo* pAuth = jget(pS->pProto, "authorization");
	if(pAuth == NULL) pAuth = jget(pS->pProto, "authentication");
	if(pAuth == NULL){ fputs("Auth:     unknown\n", pOut); return; }
	if(!jtrue(jget(pAuth, "required"))){ fputs("Auth:     not required\n", pOut); return; }
	fputs("Auth:     REQUIRED", pOut);
	if(jstr(pAuth, "realm")) fprintf(pOut, ", realm \"%s\"", jstr(pAuth, "realm"));
	const DasJdo* pMeth = jget(pAuth, "methods");
	if(jisary(pMeth)){
		fputs(" via", pOut);
		for(const das_json_ary_el* pEl = DasJdo_aryFirst(pMeth); pEl; pEl = pEl->next)
			fprintf(pOut, " %s", DasJdo_string(pEl->value) ? DasJdo_string(pEl->value) : "?");
	}
	if(jstr(pAuth, "description")) fprintf(pOut, "\n          %s", jstr(pAuth, "description"));
	fputs("\n", pOut);
}

static void prnFormats(const DasHttpSrc* pS, FILE* pOut)
{
	const DasJdo* pFmts = jget(pS->pIface, "formats");
	if(jisdict(pFmts)){
		fputs("Formats: ", pOut);
		for(const das_json_dict_el* pEl = DasJdo_dictFirst(pFmts); pEl; pEl = pEl->next){
			fprintf(pOut, " %s", pEl->name->string);
			if(jtrue(jget(pEl->value, "props/enabled/value"))) fputs(" (default)", pOut);
		}
		fputs("\n", pOut);
		return;
	}
	const char* sMime = jstr(pS->pProto, "format/default/mime");
	if(sMime == NULL) sMime = jstr(pS->pIface, "format/default/mime");
	if(sMime == NULL){
		const DasJdo* pDom = DasNode_getJdo(pS->pNode, NULL);
		sMime = jstr(pDom, "format/default/mime");
	}
	if(sMime) fprintf(pOut, "Formats:  %s\n", sMime);
}

static void DasHttpSrc_describe(const DasHttpSrc* pS, FILE* pOut, const char* sLabel, const char* sUri)
{
	const DasNode* pNode = pS->pNode;
	const char* sVer = DasNode_version(pNode);
	fprintf(pOut, "HttpStreamSrc%s%s:  %s\n", sVer ? " " : "", sVer ? sVer : "",
	        DasNode_label(pNode) ? DasNode_label(pNode) : "");
	prnField(pOut, "Title:", DasNode_title(pNode));
	prnField(pOut, "Summary:", DasNode_summary(pNode));
	prnField(pOut, sLabel, sUri);
	if(g_bVerbose){
		prnField(pOut, "From:", DasNode_srcUrl(pNode));
		const char* sBase = DasHttpSrc_baseUrl(pS, 0);
		if(sBase){
			fprintf(pOut, "Server:   %s", sBase);
			if(DasHttpSrc_baseCount(pS) > 1) fprintf(pOut, " (+%zu alternate)", DasHttpSrc_baseCount(pS) - 1);
			fputs("\n", pOut);
		}
	}
	prnAuth(pS, pOut);
	prnFormats(pS, pOut);

	if(g_uShow & SHOW_EXAMPLES) DasHttpSrc_printExamples(pS, pOut);

	if(pS->uSet == 0){
		fputs("\nThis source takes no query parameters.\n", pOut);
		return;
	}

	if(g_bVerbose) fputs("\nQuery keys, use as KEY=VALUE with 'data' or 'url':\n", pOut);

	/* One table per section, keys shown without their section prefix since
	   the heading carries it.  Column widths are per section so the long
	   enum lists of the formats section do not push everything else over. */
	enum { C_KEY, C_TYPE, C_UNITS, C_DEF, C_REQ, C_VALS, C_DESC, C_COUNT };
	const char* aHdr[C_COUNT] = {"Key", "Type", "Units", "Default", "Req", "Values", "Description"};
	char sVals[80];
	char sDesc[256];

	for(int iSec = 0; iSec < SEC_COUNT; ++iSec){
		sec_e sec = (sec_e)iSec;
		size_t uInSec = 0;
		for(size_t u = 0; u < pS->uSet; ++u) if(pS->aSet[u].sec == sec) ++uInSec;
		if(uInSec == 0) continue;
		if(!(g_uShow & (1u << sec))) continue;

		size_t aW[C_COUNT];
		for(int c = 0; c < C_COUNT; ++c) aW[c] = strlen(aHdr[c]);
		for(size_t u = 0; u < pS->uSet; ++u){
			const DasSettable* p = pS->aSet + u;
			if(p->sec != sec) continue;
			const char* sShort = strchr(p->sKey, '.') ? strchr(p->sKey, '.') + 1 : p->sKey;
			DasSettable_values(p, sVals, sizeof(sVals));
			size_t a[C_COUNT] = {
				strlen(sShort), strlen(DasSettable_type(p)), strlen(DasSettable_units(p)),
				strlen(p->sDefault), 3, strlen(sVals), 0
			};
			for(int c = 0; c < C_DESC; ++c) if(a[c] > aW[c]) aW[c] = a[c];
		}
		int nDescCol = 2;
		for(int c = 0; c < C_DESC; ++c) nDescCol += (int)aW[c] + 2;

		fprintf(pOut, "\n%s:\n", g_sSecName[sec]);
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

		for(size_t u = 0; u < pS->uSet; ++u){
			const DasSettable* p = pS->aSet + u;
			if(p->sec != sec) continue;
			const char* sShort = strchr(p->sKey, '.') ? strchr(p->sKey, '.') + 1 : p->sKey;
			DasSettable_values(p, sVals, sizeof(sVals));
			fprintf(pOut, "  %-*s  %-*s  %-*s  %-*s  %-*s  %-*s  ",
			        (int)aW[C_KEY], sShort, (int)aW[C_TYPE], DasSettable_type(p),
			        (int)aW[C_UNITS], DasSettable_units(p), (int)aW[C_DEF], p->sDefault,
			        (int)aW[C_REQ], p->bRequired ? "yes" : "", (int)aW[C_VALS], sVals);
			fputsWrapped(DasSettable_desc(p, sDesc, sizeof(sDesc)), nDescCol, pOut);
			fputc('\n', pOut);
		}
	}
	/* Usage hints are for the first dozen runs, not the hundredth.  They
	   live in 'data -h' and come back with -v. */
	if(!g_bVerbose) return;

	/* Shortcut hints specific to this source */
	fputs("\nShortcuts:\n", pOut);
	char sLastGroup[NAME_SZ] = {'\0'};
	for(size_t u = 0; u < pS->uSet; ++u){
		const DasSettable* p = pS->aSet + u;
		if((p->sec != SEC_COORD)||strieq(p->sGroup, sLastGroup)) continue;
		strcpyz(sLastGroup, p->sGroup, NAME_SZ);
		bool bMin = DasHttpSrc_hasProp(pS, SEC_COORD, p->sGroup, "min");
		bool bMax = DasHttpSrc_hasProp(pS, SEC_COORD, p->sGroup, "max");
		if(!bMin || !bMax) continue;
		const char* sStep = DasHttpSrc_hasProp(pS, SEC_COORD, p->sGroup, "res") ? "RES" :
		                    DasHttpSrc_hasProp(pS, SEC_COORD, p->sGroup, "inter") ? "INTER" : NULL;
		fprintf(pOut, "  %s=MIN,MAX%s%s\n", p->sGroup, sStep ? "," : "", sStep ? sStep : "");
	}
	prnPara(pOut, 2, "KEY alone sets a boolean key true, N alone starts from example N, and the "
	        "section prefix (coords., data., ...) may be dropped when unambiguous.  "
	        "See '" PROG " data -h'.");
}

/* ************************************************************************* */
/* Listing a catalog */

static void listCatalog(const DasNode* pNode, FILE* pOut, const char* sLabel, const char* sUri)
{
	const char* sType = jstr(DasNode_getJdo(pNode, NULL), D2FRAG_TYPE);
	const char* sVer = DasNode_version(pNode);
	fprintf(pOut, "%s%s%s:  %s\n", sType ? sType : "Node", sVer ? " " : "", sVer ? sVer : "",
	        DasNode_label(pNode) ? DasNode_label(pNode) : "");
	prnField(pOut, "Title:", DasNode_title(pNode));
	prnField(pOut, sLabel, sUri);
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

/* What to show as a node's name.  Every node has a catalog URI, nodes
 * loaded by location carry a local: one, see D2URI_LOCAL.  Catalogs get
 * their child separator appended so the next step is obvious. */
static const char* nodeUri(
	const DasNode* pNode, const char* sFallback, char* sBuf, size_t uLen
){
	const char* sUri = DasNode_pathUri(pNode);
	if((sUri == NULL)||(sUri[0] == '\0')) sUri = sFallback;
	const char* sSep = DasNode_isCatalog(pNode) ? DasNode_pathSep(pNode) : NULL;
	snprintf(sBuf, uLen, "%s%s", sUri, sSep ? sSep : "");
	return "URI:";
}

/* ************************************************************************* */
/* Catalog navigation
 *
 * DasCat (das3/catalog.h) owns the roots, the credentials, the agent and the
 * memory of every node loaded, and resolves paths under one root policy.
 * What stays here is the command line spelling of that policy and the
 * wording of a failed lookup. */


/* A -r ROOT or -m URI=URL value, or their environment stand-ins, turned into
 * catalog policy.  URI=URL names a node: the whole catalog for -r, a mount
 * on the federated catalog for -m.  For -r anything else is a URI prefix.
 * A location alone is refused for both: nodes do not know their own
 * names, so a file offered as a root with no URI would get a local: name
 * and its requisitions would never match the catalog node it copies.
 * The URL side always has a scheme or looks like a file, which is how the
 * '=' that matters is found when the URI itself contains one.  Command line
 * mistakes are reported here, the catalog logs its own refusals.  False on
 * either. */
static bool applyRootArg(DasCat* pCat, const char* sVal, bool bMount)
{
	for(const char* pEq = strchr(sVal, '='); pEq != NULL; pEq = strchr(pEq + 1, '=')){
		if(!das_isUrlOrFile(pEq + 1)) continue;
		if(pEq == sVal){                               /* "=URL", no name */
			fprintf(stderr, PROG ": %s needs URI=URL, a node is named by where it sits "
			        "in the catalog\n", bMount ? "-m" : "-r");
			return false;
		}
		char sUri[URI_SZ];
		size_t uLen = (size_t)(pEq - sVal);
		if(uLen >= sizeof(sUri)) uLen = sizeof(sUri) - 1;
		memcpy(sUri, sVal, uLen);
		sUri[uLen] = '\0';
		if(bMount) return DasCat_mount(pCat, pEq + 1, sUri) == DAS_OKAY;
		return DasCat_setRoot(pCat, pEq + 1, sUri) == DAS_OKAY;
	}
	if(bMount){
		fprintf(stderr, PROG ": -m needs URI=URL, see -h\n");
		return false;
	}
	if(strchr(sVal, '=') != NULL){
		fprintf(stderr, PROG ": the part after '=' must be a URL or a file\n");
		return false;
	}
	if(das_isUrlOrFile(sVal)){
		fprintf(stderr, PROG ": a root needs its catalog URI, -r URI=%s, see -h\n", sVal);
		return false;
	}
	return DasCat_setPrefix(pCat, sVal) == DAS_OKAY;
}

/* From a node the user named, get the HttpStreamSrc to query.  Containers
 * of sources pick a child: 'flex', else the first stream source listed.  To
 * pick another the user puts its name on the end of the path. */
static DasNode* pickSource(DasNode* pNode, char* sErr, size_t uErr)
{
	if(DasNode_isStreamSrc(pNode)) return pNode;

	if(!DasNode_isCatalog(pNode)){
		snprintf(sErr, uErr, "Node %s is not a data source", DasNode_srcUrl(pNode));
		return NULL;
	}

	size_t uKids = DasNode_subCount(pNode);
	const char* sPick = NULL;
	bool bHasCatalogs = false;

	for(size_t u = 0; u < uKids; ++u){
		const char* sName = DasNode_subName(pNode, u);
		das_node_type_e nType = DasNode_subType(pNode, u);
		if((nType == d2node_catalog)||(nType == d2node_spdf_cat)||(nType == d2node_spase_cat))
			bHasCatalogs = true;
		if(nType != d2node_stream_src) continue;
		if(strieq(sName, "flex")){ sPick = sName; break; }
		if(sPick == NULL) sPick = sName;
	}

	if(sPick == NULL){
		if(bHasCatalogs)
			snprintf(sErr, uErr, "This is a catalog, not a data source.  Run '" PROG
			         " node' on it and pick a child");
		else
			snprintf(sErr, uErr, "No HttpStreamSrc children under %s", DasNode_srcUrl(pNode));
		return NULL;
	}

	/* Credentials and agent are inherited from the node's root */
	DasNode* pSub = DasNode_subNode(pNode, sPick, NULL, NULL);
	if(pSub == NULL){
		snprintf(sErr, uErr, "Couldn't load source '%s' from %s", sPick, DasNode_srcUrl(pNode));
		return NULL;
	}
	if(!DasNode_isStreamSrc(pSub)){
		snprintf(sErr, uErr, "Child '%s' loaded but is not an HttpStreamSrc", sPick);
		return NULL;
	}
	return pSub;
}

/* A path did not resolve.  Say whether the policy or the catalog is to
 * blame, and for the catalog, which child was the problem and what its
 * parent offers.  Call right after the failed DasCat_resolve(). */
static void explainUnresolved(DasCat* pCat, const char* sPath)
{
	char sFullUri[URI_SZ];
	strcpyz(sFullUri, DasCat_lastUri(pCat), sizeof(sFullUri));

	/* The common mistake with a named root is asking for something it does
	   not cover, say so instead of hunting for a parent.  A local: name is
	   never looked up at all, see D2URI_LOCAL. */
	if(!DasCat_covers(pCat, sFullUri)){
		if(DasCat_rootUri(pCat) == NULL)
			daslog_error_v("Nothing is mounted at '%s', and local: names are not looked up "
			               "in the federated catalog", sFullUri);
		else
			daslog_error_v("'%s' is outside the -r root %s, use -m to mount it on the "
			               "federated catalog instead", sFullUri, DasCat_rootUri(pCat));
		return;
	}

	const char* sRest = NULL;
	DasNode* pNode = DasCat_nearest(pCat, &sRest);
	if((pNode == NULL)||(sRest == NULL)||(sRest[0] == '\0')){
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

static bool isDigits(const char* s)
{
	if((s == NULL)||(*s == '\0')) return false;
	for(; *s; ++s) if(!isdigit((unsigned char)*s)) return false;
	return true;
}

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
		sec_e sec;
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
	const char* sRootArg = NULL;
	const char* sMountArg = NULL;
	bool bHelp = false;
	bool bFresh = false;
	bool bQuiet = false;
	const char* aKeyVals[MAX_SETTABLES];
	size_t uKeyVals = 0;
	DasHttpParam aRaw[MAX_RAW];
	size_t uRaw = 0;
	int nExample = -1;
	float rTimeout = 10.0;
	char sErr[ERR_SZ] = {'\0'};

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
		if(strcmp(sArg, "--root") == 0) sArg = "-r";
		if(strcmp(sArg, "--mount") == 0) sArg = "-m";
		if((strcmp(sArg, "-r") == 0)||(strcmp(sArg, "-m") == 0)||(strcmp(sArg, "-s") == 0)||
		   (strcmp(sArg, "-C") == 0)||(strcmp(sArg, "-p") == 0)||(strcmp(sArg, "-t") == 0)||
		   (strcmp(sArg, "-l") == 0)){
			if(i + 1 >= argc){
				fprintf(stderr, PROG ": option %s needs a value, use -h for help\n", sArg);
				return RET_USAGE;
			}
			const char* sVal = argv[++i];
			switch(sArg[1]){
			case 'r': sRootArg = sVal; break;
			case 'm': sMountArg = sVal; break;
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
				size_t uName = (size_t)(pEq - sVal);
				if(uName >= NAME_SZ) uName = NAME_SZ - 1;
				memcpy(aRaw[uRaw].sName, sVal, uName);
				aRaw[uRaw].sName[uName] = '\0';
				strcpyz(aRaw[uRaw].sVal, pEq + 1, VAL_SZ);
				++uRaw;
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
		if(uKeyVals < MAX_SETTABLES) aKeyVals[uKeyVals++] = sArg;
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
	   catalog.  The root is applied first so a mount can complain about a
	   conflicting one. */
	const char* sEnv = getenv(ROOT_ENV);
	if((sRootArg == NULL) && (sEnv != NULL) && (sEnv[0] != '\0')) sRootArg = sEnv;
	sEnv = getenv(MOUNT_ENV);
	if((sMountArg == NULL) && (sEnv != NULL) && (sEnv[0] != '\0')) sMountArg = sEnv;
	if((sRootArg != NULL) && !applyRootArg(pCat, sRootArg, false))
		return finish(pCat, RET_USAGE);
	if((sMountArg != NULL) && !applyRootArg(pCat, sMountArg, true))
		return finish(pCat, RET_USAGE);

	if(sCmd == NULL){
		fprintf(stderr, PROG ": no command given, use -h for help\n");
		return finish(pCat, RET_USAGE);
	}

	if(strcmp(sCmd, "roots") == 0){
		size_t uRoots = 0;
		const char** psRoots = das_root_urls(&uRoots);
		for(size_t u = 0; u < uRoots; ++u) printf("%s\n", psRoots[u]);
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
	if((bJson || bUrl || bData) && (sPath == NULL) && !DasCat_hasRoot(pCat) && (DasCat_mountCount(pCat) == 0)){
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
	const char* sFullUri = DasCat_lastUri(pCat);
	char sShow[1200];
	const char* sLabel = nodeUri(pNode, sFullUri, sShow, sizeof(sShow));

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
			if(!bSrcSet || g_bVerbose) listCatalog(pNode, stdout, sLabel, sShow);

			if(bSrcSet){
				DasNode* pSrc = pickSource(pNode, sErr, sizeof(sErr));
				if(pSrc == NULL){
					daslog_error_v("%s", sErr);
					nRet = RET_NODE;
				}
				else{
					DasHttpSrc src;
					if(!DasHttpSrc_init(&src, pSrc, sErr, sizeof(sErr))){
						daslog_error_v("%s", sErr);
						nRet = RET_NODE;
					}
					else if(!g_bVerbose){
						DasHttpSrc_describe(&src, stdout, sLabel, sShow);
					}
					else{
						/* The child's name is the tail of its path URI, after the
						   separator this container uses */
						const char* sSrcUri = DasNode_pathUri(pSrc);
						const char* sSep = DasNode_pathSep(pNode);
						const char* sName = sSrcUri;
						if(sSrcUri && sSep && sSep[0]){
							const char* p = strstr(sSrcUri, sSep);
							while(p){ sName = p + strlen(sSep); p = strstr(sName, sSep); }
						}
						char sNote[256];
						snprintf(sNote, sizeof(sNote), "Default source: %s", sName ? sName : "first");
						fputc('\n', stdout);
						prnPara(stdout, 0, sNote);
						fputc('\n', stdout);
						char sSrcShow[1200];
						const char* sSrcLabel = nodeUri(pSrc, sSrcUri ? sSrcUri : "", sSrcShow, sizeof(sSrcShow));
						DasHttpSrc_describe(&src, stdout, sSrcLabel, sSrcShow);
					}
				}
			}
		}
		else if(DasNode_isStreamSrc(pNode)){
			DasHttpSrc src;
			if(!DasHttpSrc_init(&src, pNode, sErr, sizeof(sErr))){
				daslog_error_v("%s", sErr);
				nRet = RET_NODE;
			}
			else
				DasHttpSrc_describe(&src, stdout, sLabel, sShow);
		}
		else{
			const char* sType = jstr(DasNode_getJdo(pNode, NULL), D2FRAG_TYPE);
			printf("%s:  %s\n%-10s%s\n", sType ? sType : "Node",
			       DasNode_label(pNode) ? DasNode_label(pNode) : "", sLabel, sShow);
			if(g_bVerbose) printf("From:     %s\n", DasNode_srcUrl(pNode));
			printf("\n" PROG " has no detailed printer for this node type, try the json command.\n");
		}
				return finish(pCat, nRet);
	}

	/* url or data */
	DasNode* pSrc = pickSource(pNode, sErr, sizeof(sErr));
	if(pSrc == NULL){
		daslog_error_v("%s", sErr);
				return finish(pCat, RET_NODE);
	}

	DasHttpSrc src;
	if(!DasHttpSrc_init(&src, pSrc, sErr, sizeof(sErr))){
		daslog_error_v("%s", sErr);
				return finish(pCat, RET_NODE);
	}

	/* Order matters: example first, then explicit keys override it, and -p
	   raw parameters have the last word.  A 0.6 example is raw protocol
	   parameters, those go straight into the output list before the keys
	   are translated on top of them. */
	DasHttpParam aExRaw[MAX_RAW];
	size_t uExRaw = 0;

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
	if((nExample < 0) && (uKeyVals == 0) && (uRaw == 0) && (DasHttpSrc_exampleCount(&src) > 0)){
		nExample = 0;
		if(!bQuiet)
			fprintf(stderr, PROG ": no query given, using example 0, \"%s\"\n", DasHttpSrc_exampleLabel(&src, 0));
	}

	if(nExample >= 0){
		if(!DasHttpSrc_applyExample(&src, (size_t)nExample, aExRaw, &uExRaw, MAX_RAW, sErr, sizeof(sErr))){
			daslog_error_v("%s", sErr);
						return finish(pCat, RET_QUERY);
		}
	}
	for(size_t u = 0; u < uExRaw; ++u){
		if(!DasHttpSrc_outSet(&src, aExRaw[u].sName, aExRaw[u].sVal, sErr, sizeof(sErr))){
			daslog_error_v("%s", sErr);
						return finish(pCat, RET_QUERY);
		}
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
		if(!DasHttpSrc_set(&src, sKey, sVal, sErr, sizeof(sErr))){
			daslog_error_v("%s", sErr);
						return finish(pCat, RET_QUERY);
		}
	}

	if(!DasHttpSrc_build(&src, sErr, sizeof(sErr))){
		daslog_error_v("%s", sErr);
				return finish(pCat, RET_QUERY);
	}
	for(size_t u = 0; u < uRaw; ++u){
		if(!DasHttpSrc_outSet(&src, aRaw[u].sName, aRaw[u].sVal, sErr, sizeof(sErr))){
			daslog_error_v("%s", sErr);
						return finish(pCat, RET_QUERY);
		}
	}
	if(!DasHttpSrc_checkRequired(&src, sErr, sizeof(sErr))){
		daslog_error_v("%s", sErr);
				return finish(pCat, RET_QUERY);
	}

	char sUrl[URL_SZ];
	if(bUrl){
		if(!DasHttpSrc_url(&src, 0, sUrl, sizeof(sUrl))){
			daslog_error("Query URL too long or no base URL");
			nRet = RET_QUERY;
		}
		else{
			printf("%s\n", sUrl);
		}
				return finish(pCat, nRet);
	}

	/* data: try each base URL in turn until one answers.  A server that
	   answers with an error is an answer, only connection failures move on */
	size_t uBases = DasHttpSrc_baseCount(&src);
	nRet = RET_SERVER;
	for(size_t u = 0; u < uBases; ++u){
		if(!DasHttpSrc_url(&src, u, sUrl, sizeof(sUrl))){
			daslog_error("Query URL too long");
			nRet = RET_QUERY;
			break;
		}
		nRet = streamUrl(pCat, sUrl, rTimeout);
		if((nRet != RET_SERVER)||(u + 1 >= uBases)) break;
		daslog_warn_v("Trying alternate server %s", DasHttpSrc_baseUrl(&src, u + 1));
	}

	finish(pCat, nRet);
	das_finish();
	return nRet;
}
