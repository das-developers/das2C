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
#define strncasecmp _strnicmp
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <strings.h>               /* strncasecmp */
#endif

#define PROG "das_fetch"
#define PERR (DASERR_MAX + 1)

#define CRED_FILE     "servers"    /* under dasfs_userDir(); the older ~/.dasauth is not read */

#ifndef _WIN32
#define HOME_VAR "$HOME"           /* for help text, dasfs_userDir() does the lookup */
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
 * by dasid_check(), at most 63 characters, and a full key is
 * "section.group.prop", so KEY_SZ comes from that rule, not from a guess. */
#define MAX_KEYS 512    /* KEY=VALUE arguments on one command line */
#define MAX_RAW   32    /* -p arguments */
#define KEY_SZ   (8 + 2*DASID_SZ)
#define NAME_SZ  DASID_SZ
#define URI_SZ 1024
#define URL_SZ 4096
#define ERR_SZ 1024

#define SEED_ENV  "DASFETCH_SEED"
#define MOUNT_ENV "DASFETCH_MOUNT"
#define MAX_MOUNTS 16
#define MAX_KIDS 32

/* What to take when a catalog node offers several sources.  Order:
   das3, then das2, then anything since we only copy out bytes.  One ask
   per type so das3 wins over das2 instead of tying with it; a -t list is
   one ask for the whole set. */
#define DEF_ACCEPT_DAS3_BIN  "application/vnd.das.stream"
#define DEF_ACCEPT_DAS3_TEXT "text/vnd.das.stream"
#define DEF_ACCEPT_DAS2_BIN  "application/vnd.das2.das2stream"
#define DEF_ACCEPT_DAS2_TEXT "text/vnd.das2.das2stream"

/* ************************************************************************* */
/* Help */

static void prnHelpMain(void)
{
	fputs(
"NAME\n"
"   " PROG " - fetch data by das catalog paths, not absolute locations\n"
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
"   The das federated catalog is a tree of small JSON documents called\n"
"   nodes.  Catalog nodes list other nodes, and data source nodes describe\n"
"   how to query a server.  " PROG " follows a path through that tree for\n"
"   you, so you do not need to know where a server is or how its query\n"
"   URLs are built.\n"
"\n"
"   At the terminal, use 'node' to look around and then 'data' to download.\n"
"   The 'data' command writes the server's reply to standard output, which\n"
"   can be redirected to a file or piped to das3_csv, das2_psd and similar\n"
"   programs.  The reply is not altered, so the output format is whatever\n"
"   the source produces: a das2 or das3 stream, CDF, CSV, or raw packets.\n"
"\n"
"COMMANDS\n"
"   seeds            Print the built in URLs of the das catalog root.\n"
"   node [PATH]      List the children of a catalog, or the examples and\n"
"                    query keys of a data source.  Without PATH, show the\n"
"                    node where paths start.\n"
"   json PATH        Print a node's JSON document, or one part of it.\n"
"   url  PATH ...    Print the URL a query turns into.\n"
"   data PATH ...    Run a query and write the reply to standard output.\n"
"\n"
"   Each command has its own help page.  '" PROG " data -h' explains query\n"
"   keys and '" PROG " node -h' explains the listings.\n"
"\n"
"PATHS\n"
"   A PATH names a catalog node.  Paths start at the das2 sites node,\n"
"   " DASNODE_URI_SITES ", so these two name the same node:\n"
"\n"
"      uiowa/juno/wav/survey\n"
"      " DASNODE_URI_SITES ":/uiowa/juno/wav/survey\n"
"\n"
"   The node where paths start is called the seed.  To start somewhere\n"
"   else use -s or the " SEED_ENV " variable.  To start at the top of the\n"
"   das catalog, where every path is a full name, use -r.\n"
"\n"
"OPTIONS\n"
"   -h, --help\n"
"      Print this text.  After a command, print that command's help.\n"
"\n"
"   -v, --version\n"
"      Print the program and library version and exit.\n"
"\n"
"   -l, --log LEVEL\n"
"      How much to report on standard error, one of: critical, error,\n"
"      warning, info, debug, trace.  The first letter is enough.  The\n"
"      default is info, use -l warning for quieter runs.\n"
"\n"
"   -r, --root\n"
"      Start paths at the top of the das catalog instead of at the das2\n"
"      sites.  Every path is then a full name, and catalogs other than\n"
"      das2.org's can be reached:\n"
"\n"
"         " PROG " -r node " DASNODE_URI_ROOT "test:/uiowa\n"
"\n"
"   -s, --seed SEED\n"
"      Start paths at another node.  There are three forms:\n"
"\n"
"         -s " DASNODE_URI_ROOT "test\n"
"\n"
"      A NAME alone is a node of the das catalog.  Paths are then relative\n"
"      to it, so 'uiowa' is " DASNODE_URI_ROOT "test:/uiowa.\n"
"\n"
"         -s my_site_test.json\n"
"\n"
"      A FILE alone stands for the top of a catalog, with no name of its\n"
"      own.  Paths are then names below that node.  FILE may be a URL.\n"
"\n"
"         -s my_juno_tests.json=" DASNODE_URI_SITES ":/uiowa/juno\n"
"\n"
"      FILE=NAME states which catalog node the file is.  Paths may then be\n"
"      relative to NAME, so 'wav/survey' is the node\n"
"      " DASNODE_URI_SITES ":/uiowa/juno/wav/survey.  A URL that contains\n"
"      an '=' needs this form.\n"
"\n"
"      With any form, names outside the seed are not found.  To replace\n"
"      one node and keep the rest, use -m.  -r and -s can't be used\n"
"      together.\n"
"\n"
"   -m, --mount FILE=NAME\n"
"      Use the node in FILE in place of the catalog node NAME.  Paths at\n"
"      or below NAME are answered from FILE, and the rest of the catalog\n"
"      is unchanged:\n"
"\n"
"         -m tracers-1.json=" DASNODE_URI_SITES ":/uiowa/tracers/tracers-1\n"
"\n"
"      FILE may be a URL.  -m may be repeated.\n"
"\n"
"   -o, --output OUTPUT\n"
"      What to print, for the commands that offer a choice: the sections\n"
"      of a data source for node, the layout of the URL for url.  See\n"
"      '" PROG " node -h' and '" PROG " url -h'.\n"
"\n"
"   -t, --type TYPES\n"
"      Where a path leads to several sources for the same dataset, use the\n"
"      one that provides one of these media types.  TYPES is a comma\n"
"      separated list of media types, or of these short names:\n"
"\n"
"         das3  d3b  d3t         das streams, binary or text; das3 is both\n"
"         das2  d2s  d2t         das2 streams\n"
"         das1  d1s  d1t         das1 streams\n"
"         qstream  qds  qdt      Autoplot QStreams\n"
"         ccsds                  CCSDS packets, any mission\n"
"         csv  cdf  json  png  xml\n"
"\n"
"      Other text without a '/' is looked for anywhere in the media type.\n"
"      A quoted '*/*' matches any type.  Without -t, das3 streams are\n"
"      preferred, then das2 streams, then anything else.\n"
"\n"
"   -p, --param PARAM=VALUE\n"
"      For url and data, add PARAM=VALUE to the request URL as typed,\n"
"      without checking it against the source's query keys.  May be\n"
"      repeated.\n"
"\n"
"   -u, --update\n"
"      Fetch the catalog nodes along the path again, ignoring saved\n"
"      copies, and save the new ones.  Use this after a catalog has\n"
"      changed.\n"
"\n"
"   --cache DIR\n"
"      Keep saved copies of catalog nodes in DIR instead of the default\n"
"      location, see FILES.\n"
"\n"
"   --no-cache\n"
"      Do not read or write saved copies of catalog nodes.\n"
"\n"
"   --timeout SECONDS\n"
"      How long to wait for a connection to the data server, default 10.\n"
"\n"
"   A long option's value may follow a space or an '=', as in --seed X or\n"
"   --seed=X.\n"
"\n"
"FILES\n"
"   " HOME_VAR "/.das/cache/nodes/\n"
"      Saved copies of catalog nodes fetched over the network, reused for\n"
"      one hour.  Following a path takes one request per level, so with\n"
"      these a repeat run contacts only the data server.  See -u, --cache\n"
"      and --no-cache.\n"
"\n"
"   " HOME_VAR "/.das/" CRED_FILE "\n"
"      Saved usernames and passwords.  When a server asks for a login,\n"
"      these are tried before you are prompted.\n"
"\n"
"ENVIRONMENT\n"
"   " SEED_ENV "\n"
"      A SEED value, used as if given with -s when neither -s nor -r is.\n"
"\n"
"   " MOUNT_ENV "\n"
"      One FILE=NAME value, used as if given with -m when -m is absent.\n"
"\n"
"   DAS_USERDIR\n"
"      The directory for the node cache and saved logins listed under\n"
"      FILES, in place of " HOME_VAR "/.das.\n"
"\n"
"   https_proxy, http_proxy\n"
"      The proxy to use for https and http URLs, written as\n"
"      http://[USER:PASS@]HOST[:PORT].  The upper case names are read when\n"
"      the lower case ones are not set.\n"
"\n"
"   no_proxy\n"
"      Hosts to contact directly instead of through the proxy.  A comma\n"
"      separated list of host names or domain endings, or * for all.\n"
"      Address ranges such as 10.0.0.0/8 are not recognized.  NO_PROXY is\n"
"      read when no_proxy is not set.\n"
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
"   Show what is at a catalog path.  Without PATH, show the node where\n"
"   paths start, the das2 sites node unless -r, -s or the " SEED_ENV "\n"
"   variable says otherwise.\n"
"\n"
"CATALOGS\n"
"   For a catalog the listing gives its name, title and URI, then one line\n"
"   per child: name, node type and title.  The 'Children' line shows the\n"
"   separator this catalog puts before a child's name, so the path to a\n"
"   child is the URI, the separator, then the name.  Where that line says\n"
"   'named in full' there is no separator.\n"
"\n"
"   A '*' after the type marks a kind of node that " PROG " can list but\n"
"   cannot read or download from, such as Das2DSDF and WebSocSrc.\n"
"\n"
"   A SourceSet or Collection groups several data sources for the same\n"
"   dataset.  Only the one that 'data' and 'url' would use is shown.  It\n"
"   is chosen by media type, see -t in '" PROG " -h'.  Use -o sources to\n"
"   list them all.  To use another, add its name to the path, as in\n"
"   uiowa/juno/wav/survey/das2.\n"
"\n"
"DATA SOURCES\n"
"   For a data source, node type HttpStreamSrc, the listing gives a header\n"
"   and then sections.  These are printed unless -o says otherwise:\n"
"\n"
"   examples      Ready made queries.  Key is the number to give 'data' or\n"
"                 'url' to run one.\n"
"   coords        Coordinate settings, such as the time range.\n"
"   data          Data variables that can be switched on or off.\n"
"   options       Other settings of the source.\n"
"\n"
"   These are printed only when asked for:\n"
"\n"
"   formats       Output formats and their settings.  The 'Formats:' line\n"
"                 of the header always lists the format names.\n"
"   sources       Every source of a SourceSet or Collection.\n"
"   all           Everything above.\n"
"\n"
"   All but examples and sources are tables of query keys, with these\n"
"   columns:\n"
"\n"
"   Key           What to type, shown without the section prefix.\n"
"   Type, Units   The kind of value and its units.\n"
"   Default       The value the server uses when the key is not given.\n"
"   Req           'yes' for keys the server requires.\n"
"   Values        The choices, or the allowed range.\n"
"\n"
"   To see the URL an example turns into, give its number to 'url':\n"
"   '" PROG " url PATH 0'.\n"
"\n"
"OPTIONS\n"
"   -o, --output SECTIONS\n"
"      Print only these sections, comma separated, from the lists above.\n"
"\n"
"EXAMPLES\n"
"   " PROG " node\n"
"   " PROG " node uiowa/juno/wav\n"
"   " PROG " node -o examples,coords uiowa/tracers/tracers-2/spice/geomagcoords\n"
"\n"
"   A development server's copy of the TRACERS catalog:\n"
"\n"
"   DEV=https://tracers-dev.physics.uiowa.edu/stream/root.json\n"
"   " PROG " -s $DEV=" DASNODE_URI_SITES ":/uiowa/tracers node\n"
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
"   Run a query and write the server's reply to standard output unchanged,\n"
"   so the output format is whatever the source produces.  Messages go to\n"
"   standard error.  If the server asks for a login, the prompt is read\n"
"   from the terminal, not standard input.  Exit code 0 means the whole\n"
"   reply arrived.\n"
"\n", stdout);
	else
		fputs(
"   Print the URL that 'data' would request for the same arguments, without\n"
"   requesting it.  Useful in scripts, and for checking what a KEY=VALUE\n"
"   argument turns into.\n"
"\n", stdout);
	fputs(
"QUERY KEYS\n"
"   Each source defines its own query keys, so they differ from one source\n"
"   to the next.  Run '" PROG " node PATH' to see them.  Full keys look\n"
"   like:\n"
"\n"
"      coords.time.min=2026-03-01    a coordinate setting\n"
"      data.lshell.enabled=true      a data variable setting\n"
"      formats.csv.delimiter=Tab     an output format setting\n"
"      options.decompress=true       a source option\n"
"\n"
"   Shorter forms are accepted as long as the meaning is unambiguous:\n"
"\n"
"      time.min=2026-03-01           leave off the section name\n"
"      time=2026-03-01,2026-03-02    a coordinate range, MIN,MAX[,STEP].\n"
"                                    STEP is the resolution if the\n"
"                                    coordinate has one, otherwise the\n"
"                                    interval.  Leave an element empty to\n"
"                                    skip it: time=,2026-03-02\n"
"      lshell=true  or just  lshell  turn on a data variable or a boolean\n"
"                                    option\n"
"      nix=bad_data,hf1pwr           several values for a key whose type\n"
"                                    is a list, separated by commas.\n"
"                                    Quote the argument if you put blanks\n"
"                                    after the commas.\n"
"      csv  or  formats.csv          select an output format.  Formats\n"
"                                    are one of several: the last one\n"
"                                    named is used, and settings of the\n"
"                                    others (csv.delimiter=Tab with cdf)\n"
"                                    are refused\n"
"      delimiter=                    an empty value takes back what an\n"
"                                    example or an earlier key set\n"
"      1  or  examples.1             start from example 1 of the source,\n"
"                                    then apply the other keys to it\n"
"\n"
"   The names min / minimum, max / maximum, res / resolution and\n"
"   inter / interval are interchangeable.  The range form works for any\n"
"   coordinate a source defines, not only time.\n"
"\n"
"   With no keys at all example 0 is run, so 'data PATH' alone always\n"
"   produces something to look at.\n"
"\n"
"   A key set to its default value is not sent to the server, unless the\n"
"   server requires it.  A required key with no default must be given.  If\n"
"   one is missing, " PROG " stops before contacting the server and names\n"
"   the key to set.\n"
"\n"
"   A source that can send both das2 and das3 streams has a key to choose\n"
"   between them, usually version=das2 or version=das3.  Its default is\n"
"   what the server sends when the key is not given.\n"
"\n"
"OPTIONS\n", stdout);
	if(!bData)
		fputs(
"   -o, --output FORM\n"
"      How to print the URL, one of:\n"
"\n"
"         url      The whole URL on one line, ready to paste.  The\n"
"                  default.\n"
"         params   The source's base URLs, one per line, in the order\n"
"                  'data' tries them.  Then each parameter the query adds,\n"
"                  on a line of its own, indented three spaces and\n"
"                  decoded.  For reading, not for pasting.\n"
"\n", stdout);
	fputs(
"   -p, --param PARAM=VALUE\n"
"      Add PARAM=VALUE to the request URL as typed, without checking it\n"
"      against the source's query keys.  May be repeated.  This ties a\n"
"      script to one server's URL format, so prefer KEY=VALUE when you can.\n"
"\n"
"   --timeout SECONDS\n"
"      How long to wait for a connection to the data server, default 10.\n"
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
"      DEV=https://tracers-dev.physics.uiowa.edu/stream/root.json\n"
"      " PROG " -s $DEV=" DASNODE_URI_SITES ":/uiowa/tracers \\\n"
"         data tracers-2/ql/mag/hsk time=2026-01-01,2026-01-02 sprt | das3_csv\n"
"\n"
"   The URL a query turns into, without running it:\n"
"\n"
"      " PROG " url uiowa/juno/wav/survey time=2021-02-21,2021-02-22 b\n"
"\n"
"   The same query, one parameter per line:\n"
"\n"
"      " PROG " url -o params uiowa/juno/wav/survey time=2021-02-21,2021-02-22 b\n"
"      https://jupiter.physics.uiowa.edu/das/server?server=dataset&dataset=Juno/WAV/Survey\n"
"         end_time=2021-02-22\n"
"         start_time=2021-02-21\n"
"         params=B\n"
"\n", stdout);
}

static void prnHelpJson(void)
{
	fputs(
"USAGE\n"
"   " PROG " [options] json PATH[#FRAGMENT]\n"
"\n"
"   Print a node's JSON document, indented for reading.  With #FRAGMENT,\n"
"   print only that part of it.  FRAGMENT is a slash separated list of\n"
"   element names.  This shows exactly what the catalog says when a 'node'\n"
"   listing leaves a question open.\n"
"\n"
"EXAMPLES\n"
"   " PROG " json uiowa/juno/wav/survey\n"
"   " PROG " json 'uiowa/juno/wav/survey/das2#protocol/http_params'\n"
"   " PROG " json 'uiowa/juno/wav/survey/das2#protocol/examples'\n"
"\n", stdout);
}

static void prnHelpSeeds(void)
{
	fputs(
"USAGE\n"
"   " PROG " seeds\n"
"\n"
"   Print the built in URLs of the das catalog root, one per line.  They\n"
"   are mirrors of one another, and the first that answers is used.  Paths\n"
"   start at the das2 sites node below this root unless -r, -s or the\n"
"   " SEED_ENV " variable says otherwise, and -m or " MOUNT_ENV " can\n"
"   replace parts of the catalog.  See '" PROG " -h'.\n"
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
	return (p == NULL) ? NULL : DasJdo_str(DasJdo_subtree(p, sPath));
}

/* Section names as typed in -o lists, singular or plural */
static bool secAlias(const char* s, das_srcsec* pSec)
{
	if(strieq(s,"coord")||strieq(s,"coords")||strieq(s,"coordinate")||strieq(s,"coordinates")){
		*pSec = DASSRCSEC_COORD; return true;
	}
	if(strieq(s,"data")){ *pSec = DASSRCSEC_DATA; return true; }
	if(strieq(s,"format")||strieq(s,"formats")||strieq(s,"fmt")){ *pSec = DASSRCSEC_FORMAT; return true; }
	if(strieq(s,"option")||strieq(s,"options")||strieq(s,"opt")||strieq(s,"opts")){
		*pSec = DASSRCSEC_OPTION; return true;
	}
	return false;
}

/* ************************************************************************* */
/* Display settings and terminal wrapping */

/* Which parts of a source 'node' prints, see -o: one bit per section,
   then the examples, then every source of a container */
#define SHOW_EXAMPLES (1u << DASSRCSEC_SIZE)
#define SHOW_SOURCES  (1u << (DASSRCSEC_SIZE + 1))
#define SHOW_ALL      ((1u << (DASSRCSEC_SIZE + 2)) - 1)
static unsigned g_uShow = SHOW_ALL & ~((1u << DASSRCSEC_FORMAT) | SHOW_SOURCES);
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

/* ************************************************************************* */
/* Describing a source.  The library has the facts, these functions only
 * decide what goes in which column. */

/* The node's spelling of a type when it gave one, else a name for what
 * the library inferred */
static const char* propTypeName(const DasSrcProp* p)
{
	/* A list is named for its items, KEY=a,b sets one */
	if(DasSrcProp_multi(p, NULL, NULL)){
		switch(DasSrcProp_type(p)){
		case DASSRCPROPTYPE_INT:  return "integer list";
		case DASSRCPROPTYPE_REAL: return "real list";
		case DASSRCPROPTYPE_TIME: return "isotime list";
		case DASSRCPROPTYPE_ENUM: return "enum list";
		default:               return "string list";
		}
	}
	const char* s = DasSrcProp_typeName(p);
	if((s != NULL)&&(s[0] != '\0')) return s;
	switch(DasSrcProp_type(p)){
	case DASSRCPROPTYPE_BOOL: return "boolean";
	case DASSRCPROPTYPE_INT:  return "integer";
	case DASSRCPROPTYPE_REAL: return "real";
	case DASSRCPROPTYPE_TIME: return "isotime";
	case DASSRCPROPTYPE_ENUM: return "enum";
	default:               return "string";
	}
}

/* How many items a list may have, when the node bounds it: " (up to 24)".
   Appended to the allowed values only if it fits whole. */
static void propCount(const DasSrcProp* p, char* sBuf, size_t uLen)
{
	size_t uLo = 0, uHi = 0;
	if(!DasSrcProp_multi(p, &uLo, &uHi)) return;
	char sCount[48] = {'\0'};
	if(uHi == SIZE_MAX){
		if(uLo > 1) snprintf(sCount, sizeof(sCount), "(%zu or more)", uLo);
	}
	else if(uLo == uHi) snprintf(sCount, sizeof(sCount), "(%zu values)", uLo);
	else if(uLo > 1)    snprintf(sCount, sizeof(sCount), "(%zu to %zu)", uLo, uHi);
	else                snprintf(sCount, sizeof(sCount), "(up to %zu)", uHi);
	size_t u = strlen(sBuf);
	if((sCount[0] == '\0')||(u + strlen(sCount) + 2 > uLen)) return;
	snprintf(sBuf + u, uLen - u, "%s%s", u ? " " : "", sCount);
}

/* Allowed values for display: enum entries, a range, or true/false */
static void propValuesOne(const DasSrcProp* p, char* sBuf, size_t uLen)
{
	sBuf[0] = '\0';
	const DasSrcCtrl* pC = DasSrcProp_ctrl(p);
	if(pC == NULL) return;

	size_t uEnum = DasSrcCtrl_numEnums(pC);
	if(uEnum > 0){
		size_t u = 0;
		for(size_t i = 0; i < uEnum; ++i){
			const char* sVal = DasSrcCtrl_enumAt(pC, i);
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
	if(DasSrcProp_type(p) == DASSRCPROPTYPE_BOOL) strcpyz(sBuf, "true, false", uLen);
}

static void propValues(const DasSrcProp* p, char* sBuf, size_t uLen)
{
	propValuesOne(p, sBuf, uLen);
	propCount(p, sBuf, uLen);
}

/* The examples table: number and what it provides.  'url PATH N' shows
   what one turns into. */
static void printExamples(const DasSrc* pSrc, FILE* pOut)
{
	size_t uEx = DasSrc_numExamples(pSrc);
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
		fputsWrapped(DasSrc_exampleLblAt(pSrc, u), nCol, pOut);
		fputc('\n', pOut);
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
	const DasJdo* pMeth = DasJdo_subtree(pAuth, "methods");
	if((pMeth != NULL)&&(pMeth->type == DASJSONTYPE_ARY)){
		fputs(" via", pOut);
		for(const DasJsonAryEl* pEl = DasJdo_aryFirst(pMeth); pEl; pEl = pEl->next)
			fprintf(pOut, " %s", DasJdo_str(pEl->value) ? DasJdo_str(pEl->value) : "?");
	}
	if(jstr(pAuth, "description")) fprintf(pOut, "\n          %s", jstr(pAuth, "description"));
	fputs("\n", pOut);
}

/* The format groups by name, or for a source without any the one MIME
 * type it produces */
static void prnFormats(const DasSrc* pSrc, FILE* pOut)
{
	size_t uGroups = DasSrc_numGroups(pSrc, DASSRCSEC_FORMAT);
	if(uGroups > 0){
		fputs("Formats: ", pOut);
		for(size_t g = 0; g < uGroups; ++g){
			const DasSrcGroup* pG = DasSrc_groupAt(pSrc, DASSRCSEC_FORMAT, g);
			fprintf(pOut, " %s", DasSrcGroup_name(pG));
			const DasSrcProp* pOn = DasSrcGroup_enableProp(pG);
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

	for(int iSec = 0; iSec < DASSRCSEC_SIZE; ++iSec){
		das_srcsec sec = (das_srcsec)iSec;
		if(!(g_uShow & (1u << sec))) continue;

		/* First pass: is there anything to show, and how wide is it */
		size_t aW[C_COUNT];
		for(int c = 0; c < C_COUNT; ++c) aW[c] = strlen(aHdr[c]);
		size_t uRows = 0;
		for(size_t g = 0; g < DasSrc_numGroups(pSrc, sec); ++g){
			const DasSrcGroup* pG = DasSrc_groupAt(pSrc, sec, g);
			for(size_t i = 0; i < DasSrcGroup_numProps(pG); ++i){
				const DasSrcProp* p = DasSrcGroup_propAt(pG, i);
				if(DasSrcProp_ctrl(p) == NULL) continue;
				++uRows;
				const char* sKey = DasSrcProp_key(p);
				const char* sShort = strchr(sKey, '.') ? strchr(sKey, '.') + 1 : sKey;
				const char* sDef = DasSrcProp_value(p);
				propValues(p, sVals, sizeof(sVals));
				size_t a[C_COUNT] = {
					strlen(sShort), strlen(propTypeName(p)), strlen(DasSrcProp_units(p)),
					sDef ? strlen(sDef) : 0, 3, strlen(sVals), 0
				};
				for(int c = 0; c < C_DESC; ++c) if(a[c] > aW[c]) aW[c] = a[c];
			}
		}
		if(uRows == 0) continue;
		int nDescCol = 2;
		for(int c = 0; c < C_DESC; ++c) nDescCol += (int)aW[c] + 2;

		fprintf(pOut, "\n%s:\n", dassrcsec_name(sec));
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

		for(size_t g = 0; g < DasSrc_numGroups(pSrc, sec); ++g){
			const DasSrcGroup* pG = DasSrc_groupAt(pSrc, sec, g);
			for(size_t i = 0; i < DasSrcGroup_numProps(pG); ++i){
				const DasSrcProp* p = DasSrcGroup_propAt(pG, i);
				const DasSrcCtrl* pC = DasSrcProp_ctrl(p);
				if(pC == NULL) continue;
				const char* sKey = DasSrcProp_key(p);
				const char* sShort = strchr(sKey, '.') ? strchr(sKey, '.') + 1 : sKey;
				const char* sDef = DasSrcProp_value(p);
				const char* sReq = DasSrcCtrl_isRequired(pC) ? "yes" : "";
				propValues(p, sVals, sizeof(sVals));
				fprintf(pOut, "  %-*s  %-*s  %-*s  %-*s  %-*s  %-*s  ",
				        (int)aW[C_KEY], sShort, (int)aW[C_TYPE], propTypeName(p),
				        (int)aW[C_UNITS], DasSrcProp_units(p), (int)aW[C_DEF], sDef ? sDef : "",
				        (int)aW[C_REQ], sReq, (int)aW[C_VALS], sVals);
				fputsWrapped(DasSrcProp_title(p, sTitle, sizeof(sTitle)), nDescCol, pOut);
				fputc('\n', pOut);
			}
		}
	}
}

static void describeSrc(const DasSrc* pSrc, FILE* pOut, const char* sUri)
{
	const char* sVer = DasSrc_version(pSrc);
	fprintf(pOut, "HttpStreamSrc%s%s:  %s\n", sVer[0] ? " " : "", sVer, DasSrc_name(pSrc));
	prnField(pOut, "Title:", DasSrc_title(pSrc));
	prnField(pOut, "Summary:", DasJdo_str(DasSrc_jdo(pSrc, DASNODE_FRAG_DESC)));
	prnField(pOut, "URI:", sUri);

	/* Where things came from is for the log, the listing is the catalog */
	daslog_debugf("%s from %s", sUri, DasSrc_url(pSrc));
	size_t uBases = DasSrc_numBaseUrls(pSrc);
	if(uBases > 0)
		daslog_debugf("Server %s, %zu alternate", DasSrc_baseUrlAt(pSrc, 0), uBases - 1);
	prnAuth(pSrc, pOut);
	prnFormats(pSrc, pOut);

	if(g_uShow & SHOW_EXAMPLES) printExamples(pSrc, pOut);

	size_t uCtrls = 0;
	for(size_t u = 0; u < DasSrc_numProps(pSrc); ++u)
		if(DasSrcProp_ctrl(DasSrc_propAt(pSrc, u)) != NULL) ++uCtrls;
	if(uCtrls == 0){
		fputs("\nThis source takes no query parameters.\n", pOut);
		return;
	}
	printKeyTables(pSrc, pOut);
}

/* ************************************************************************* */
/* Listing a catalog */

static void listCatalog(const DasNode* pNode, FILE* pOut, const char* sUri)
{
	const char* sType = jstr(DasNode_jdo(pNode, NULL), DASNODE_FRAG_TYPE);
	const char* sVer = DasNode_version(pNode);
	fprintf(pOut, "%s%s%s:  %s\n", sType ? sType : "Node", sVer ? " " : "", sVer ? sVer : "",
	        DasNode_label(pNode) ? DasNode_label(pNode) : "");
	prnField(pOut, "Title:", DasNode_title(pNode));
	prnField(pOut, "URI:", sUri);
	daslog_debugf("%s from %s", sUri, DasNode_srcUrl(pNode));

	size_t uKids = DasNode_numSubs(pNode);
	if(uKids == 0){ fputs("\nNo children listed.\n", pOut); return; }

	size_t wName = 4, wType = 4;
	for(size_t u = 0; u < uKids; ++u){
		const char* sName = DasNode_subNameAt(pNode, u);
		const char* sT = jstr(DasNode_subStubAt(pNode, u), DASNODE_FRAG_TYPE);
		if(sName && (strlen(sName) > wName)) wName = strlen(sName);
		if(sT && (strlen(sT) > wType)) wType = strlen(sT);
	}
	int nDescCol = 2 + (int)wName + 2 + (int)wType + 3;

	/* The separator is the node's, not part of its name, so it is shown
	   here, where it is used */
	const char* sSep = DasNode_pathSep(pNode);
	if(sSep[0] == '\0') fprintf(pOut, "\nChildren (%zu), named in full:\n\n", uKids);
	else fprintf(pOut, "\nChildren (%zu), after the separator '%s':\n\n", uKids, sSep);
	bool bUnsupported = false;
	for(size_t u = 0; u < uKids; ++u){
		const char* sName = DasNode_subNameAt(pNode, u);
		const DasJdo* pStub = DasNode_subStubAt(pNode, u);
		const char* sT = jstr(pStub, DASNODE_FRAG_TYPE);
		const char* sDesc = jstr(pStub, DASNODE_FRAG_TITLE);
		if(sDesc == NULL) sDesc = jstr(pStub, DASNODE_FRAG_LABEL);
		if(sDesc == NULL) sDesc = jstr(pStub, DASNODE_FRAG_DESC);
		bool bKnown = (DasNode_subTypeAt(pNode, u) != DASNODETYPE_INV);
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
 * from, else the node is called what it is.  The name alone, it can be
 * pasted back; a catalog's separator is shown with its children. */
static void nodeUri(
	const DasNode* pNode, const char* sFallback, char* sBuf, size_t uLen
){
	const char* sUri = DasNode_pathUri(pNode);
	if((sUri == NULL)||(sUri[0] == '\0')){
		if((sFallback != NULL) && (sFallback[0] != '\0')) snprintf(sBuf, uLen, "(the root, %s)", sFallback);
		else snprintf(sBuf, uLen, "(the root)");
		return;
	}
	snprintf(sBuf, uLen, "%s", sUri);
}

/* ************************************************************************* */
/* Catalog navigation
 *
 * DasCat (das3/catalog.h) owns the seed, the mounts, the credentials, the
 * agent and the memory of every node loaded, and resolves paths under one
 * policy.  What stays here is the command line spelling of that policy
 * and the wording of a failed lookup. */


/* Split FILE=NAME at the last '=' when the text before it is a location
 * and the text after it is a name.  Names are not interpreted, so a URL
 * whose query string ends in "=x" reads as named x; such a URL always
 * needs =NAME.  False, with sUrl empty, for anything else. */
static bool splitFileName(const char* sVal, char* sUrl, size_t uUrl, const char** psName)
{
	sUrl[0] = '\0';
	*psName = NULL;
	const char* pEq = strrchr(sVal, '=');
	if((pEq == NULL)||(pEq[1] == '\0')||DasCat_isNodeUrl(pEq + 1)) return false;
	size_t uLen = (size_t)(pEq - sVal);
	if(uLen >= uUrl) return false;
	memcpy(sUrl, sVal, uLen);
	sUrl[uLen] = '\0';
	if(!DasCat_isNodeUrl(sUrl)){ sUrl[0] = '\0'; return false; }
	*psName = pEq + 1;
	return true;
}

/* A -s value or its environment stand-in, sFrom says which for messages.
 * Three forms: FILE=NAME, FILE (a root), or a NAME alone, a node found
 * through the das catalog root.  The catalog logs its own refusals. */
static bool applySeedArg(DasCat* pCat, const char* sVal, const char* sFrom)
{
	char sUrl[URL_SZ];
	const char* sName = NULL;
	if(splitFileName(sVal, sUrl, sizeof(sUrl), &sName))
		return DasCat_setSeed(pCat, sUrl, sName) == DAS_OKAY;
	if(DasCat_isNodeUrl(sVal))
		return DasCat_setSeed(pCat, sVal, NULL) == DAS_OKAY;
	if(sVal[0] == '\0' || sVal[strlen(sVal) - 1] == '='){
		fprintf(stderr, PROG ": %s takes NAME, FILE or FILE=NAME, '%s' is none of them\n", sFrom, sVal);
		return false;
	}
	return DasCat_setSeed(pCat, NULL, sVal) == DAS_OKAY;
}

/* A -m value or its environment stand-in: always FILE=NAME */
static bool applyMountArg(DasCat* pCat, const char* sVal, const char* sFrom)
{
	char sUrl[URL_SZ];
	const char* sName = NULL;
	if(!splitFileName(sVal, sUrl, sizeof(sUrl), &sName)){
		fprintf(stderr, PROG ": %s takes FILE=NAME, a location then the catalog name it "
		        "stands for, '%s' is not that\n", sFrom, sVal);
		return false;
	}
	return DasCat_mount(pCat, sUrl, sName) == DAS_OKAY;
}

/* Everyday names for -t.  The extensions and type names are the ones
   dasflex servers use in their mime.json; a name stands for every media
   type listed with it.  "ccsds" needs no entry: packet types are per
   mission, vnd.nasa.tracers+ccsds-spp and the like, and the substring
   rule in stubProvides() finds them all. */
static const char* g_aTypeNames[][2] = {
	{"das3",    "application/vnd.das.stream,text/vnd.das.stream"},
	{"d3b",     "application/vnd.das.stream"},
	{"d3t",     "text/vnd.das.stream"},
	{"das2",    "application/vnd.das2.das2stream,text/vnd.das2.das2stream"},
	{"d2s",     "application/vnd.das2.das2stream"},
	{"d2t",     "text/vnd.das2.das2stream"},
	{"das1",    "application/vnd.das2.das1stream,text/vnd.das2.das1stream"},
	{"d1s",     "application/vnd.das2.das1stream"},
	{"d1t",     "text/vnd.das2.das1stream"},
	{"qstream", "application/vnd.das2.qstream,text/vnd.das2.qstream"},
	{"qds",     "application/vnd.das2.qstream"},
	{"qdt",     "text/vnd.das2.qstream"},
	{"csv",     "text/csv"},
	{"cdf",     "application/x-cdf"},
	{"json",    "application/json"},
	{"png",     "image/png"},
	{"xml",     "text/xml"}
};

/* A -t list with the everyday names swapped for their media types.  Other
   items pass through as typed.  False if the result won't fit. */
static bool expandTypes(const char* sList, char* sOut, size_t uOut)
{
	size_t uLen = 0;
	sOut[0] = '\0';
	const char* p = sList;
	while(true){
		p += strspn(p, ", \t");
		size_t uTok = strcspn(p, ", \t");
		if(uTok == 0) return true;
		const char* sAdd = NULL;
		size_t uAdd = uTok;
		for(size_t u = 0; u < sizeof(g_aTypeNames)/sizeof(g_aTypeNames[0]); ++u){
			if((strlen(g_aTypeNames[u][0]) == uTok) && (strncasecmp(p, g_aTypeNames[u][0], uTok) == 0)){
				sAdd = g_aTypeNames[u][1];
				uAdd = strlen(sAdd);
				break;
			}
		}
		if(uLen + uAdd + 2 > uOut) return false;
		if(uLen > 0) sOut[uLen++] = ',';
		memcpy(sOut + uLen, sAdd ? sAdd : p, uAdd);
		uLen += uAdd;
		sOut[uLen] = '\0';
		p += uTok;
	}
}

/* Is sNeed, uNeed characters, anywhere in the first uHay of sHay, ignoring
   case? */
static bool strnicontains(const char* sHay, size_t uHay, const char* sNeed, size_t uNeed)
{
	for(size_t u = 0; u + uNeed <= uHay; ++u)
		if(strncasecmp(sHay + u, sNeed, uNeed) == 0) return true;
	return false;
}

/* Does a child's catalog entry provide any media type in the list?  The
   list is a set compared ignoring case: an item with a '/' is a whole
   media type, one without is looked for anywhere in the type, so "csv"
   finds text/csv.  The bare wildcard, star slash star, takes anything,
   including an entry with no provides list at all.  Parameters on the
   catalog side are cut. */
static bool stubProvides(const DasJdo* pStub, const char* sAccept)
{
	const DasJdo* pProv = (pStub == NULL) ? NULL : DasJdo_subtree(pStub, "provides");
	bool bList = (pProv != NULL) && (pProv->type == DASJSONTYPE_ARY);

	const char* sWant = sAccept;
	while(true){
		sWant += strspn(sWant, ", \t");
		size_t uWant = strcspn(sWant, ", \t");
		if(uWant == 0) return false;
		if((uWant == 3) && (strncmp(sWant, "*/*", 3) == 0)) return true;
		if(bList){
			bool bWhole = (memchr(sWant, '/', uWant) != NULL);
			for(const DasJsonAryEl* pEl = DasJdo_aryFirst(pProv); pEl; pEl = pEl->next){
				const char* sHas = DasJdo_str(pEl->value);
				if(sHas == NULL) continue;
				size_t uHas = strcspn(sHas, "; \t");
				if(bWhole){
					if((uHas == uWant) && (strncasecmp(sWant, sHas, uWant) == 0)) return true;
				}
				else if(strnicontains(sHas, uHas, sWant, uWant)) return true;
			}
		}
		sWant += uWant;
	}
}

static const DasJdo* childStub(const DasNode* pNode, const char* sName)
{
	size_t uKids = DasNode_numSubs(pNode);
	for(size_t u = 0; u < uKids; ++u)
		if(strcmp(DasNode_subNameAt(pNode, u), sName) == 0) return DasNode_subStubAt(pNode, u);
	return NULL;
}

/* The library lists the children it can load; which one to fetch from
 * is this program's call, made on the media types each child provides.
 * Without -t it tries das3 streams, then das2, then anything, one ask
 * per type.
 * A -t list is one ask and a set, so a user who lists two formats gets
 * every source that emits either and names one on the path if that is
 * several.  The chosen child is loaded by the session so credentials and
 * the cache follow. */
static DasSrc* pickSrc(DasCat* pCat, DasNode* pNode, const char* sAccept)
{
	if(DasNode_isStreamSrc(pNode)) return new_DasSrc_node(pNode);

	const char* aNames[MAX_KIDS];
	size_t uKids = DasSrc_loadable(pNode, aNames, MAX_KIDS);
	if(uKids == 0){
		fprintf(stderr, PROG ": nothing under %s is a source this program can fetch from\n",
		        DasNode_pathUri(pNode));
		return NULL;
	}
	if(uKids > MAX_KIDS) uKids = MAX_KIDS;

	const char* aAsk[5] = {
		DEF_ACCEPT_DAS3_BIN, DEF_ACCEPT_DAS3_TEXT, DEF_ACCEPT_DAS2_BIN, DEF_ACCEPT_DAS2_TEXT, "*/*"
	};
	size_t uAsks = 5;
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

	das_catmiss miss;
	DasNode* pSub = DasCat_subNode(pCat, pNode, aPick[0], &miss);
	if(pSub == NULL){
		fprintf(stderr, PROG ": couldn't load source '%s' under %s: %s\n", aPick[0], DasNode_pathUri(pNode), miss.sMsg);
		return NULL;
	}
	return new_DasSrc_node(pSub);
}

/* A path did not resolve.  The library says why; what is added here is
 * the command line remedy for two mistakes it cannot know are mistakes:
 * a full URI outside the -s node, and a full URI under a -s root, whose
 * children are named by their path from it rather than by URI. */
static void explainUnresolved(
	const DasCat* pCat, const das_catmiss* pMiss, const char* sPath, bool bSites
){
	const DasNode* pNear = pMiss->pNear;
	bool bRootMiss = (pMiss->nCause == DASMISS_NOCHILD) && (pNear != NULL) &&
		(DasNode_pathUri(pNear) == NULL);

	/* With the sites node as the seed every path is relative to it, so a
	   name written from higher up is the usual slip.  The library does not
	   interpret names; this guess is the program's, only for the hint. */
	const char* sHint = NULL;
	if(bSites && (pMiss->nCause == DASMISS_NOCHILD) && (sPath != NULL)){
		if(strncmp(sPath, "site:", 5) == 0)
			sHint = "Paths start at " DASNODE_URI_SITES ", leave off the 'site:/'";
		else if(strncmp(sPath, "test:", 5) == 0)
			sHint = "The test catalog is beside the sites node, use -s " DASNODE_URI_ROOT "test";
		else if(dasurl_hasScheme(sPath))
			sHint = "Paths start at " DASNODE_URI_SITES ", use -r to give full names";
	}

	if(pMiss->nCause == DASMISS_OUTSIDE)
		daslog_errorf("%s.  Use -m to mount a file on the federated catalog "
		              "instead", pMiss->sMsg);
	else if(bRootMiss && (DasCat_seedUrl(pCat) != NULL))
		daslog_errorf("%s.  Below a -s file given no name, paths are names from "
		              "it; use -s FILE=NAME to give it one, or -m", pMiss->sMsg);
	else if(sHint != NULL)
		daslog_errorf("%s.  %s", pMiss->sMsg, sHint);
	else
		daslog_errorf("%s", pMiss->sMsg);
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
	daslog_infof("Requesting %s", sUrl);

	DasHttpResp* pRes = new_DasHttpResp_get(
		sUrl, DasCat_agent(pCat), DasCat_credMgr(pCat), rTimeout
	);
	if(pRes == NULL) return RET_SERVER;   /* out of memory, already reported */

	if(!DasHttpResp_isOk(pRes)){
		int nCode = DasHttpResp_code(pRes);
		int nRet = RET_SERVER;
		if((nCode == 401)||(nCode == 403)) nRet = RET_AUTH;
		else if(nCode == 400) nRet = RET_QUERY;
		const char* sErr = DasHttpResp_error(pRes);
		daslog_errorf("%s", sErr ? sErr : "Request failed");
		if(nCode == 400)
			daslog_error("The server rejected the query.  Check the keys with '"
			               PROG " node' or try the URL from '" PROG " url' in a browser");
		del_DasHttpResp(pRes);
		return nRet;
	}

	if(strcmp(DasHttpResp_url(pRes), sUrl) != 0)
		daslog_infof("Redirected to %s", DasHttpResp_url(pRes));
	if(DasHttpResp_mime(pRes))
		daslog_infof("Content type %s", DasHttpResp_mime(pRes));

	DasXport* pBody = DasHttpResp_takeBody(pRes);
	del_DasHttpResp(pRes);
	DasXport* pOut = new_DasXport_stdout();

	/* Each transport keeps its error message, so a failed copy says which end
	 * broke.  The output end is ours to classify, everything else is the
	 * server's fault as far as the exit code is concerned.  A full disk or
	 * a closed pipe downstream often only shows up at the final flush. */
	int64_t nTotal = 0;
	int nRet = RET_OK;
	if(DasXport_copyTo(pBody, pOut, NULL, NULL, &nTotal) != DAS_OKAY){
		if(DasXport_error(pOut) != NULL){
			daslog_errorf("Writing output, %s", DasXport_error(pOut));
			nRet = RET_WRITE;
		}
		else{
			daslog_errorf("Reading %s, %s", sUrl, DasXport_error(pBody));
			nRet = RET_SERVER;
		}
	}
	if((DasXport_close(pOut) != DAS_OKAY) && (nRet == RET_OK)){
		daslog_errorf("Writing output, %s", DasXport_error(pOut));
		nRet = RET_WRITE;
	}
	del_DasXport(pOut);
	del_DasXport(pBody);

	daslog_infof("%" PRId64 " bytes written", nTotal);
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

/* -o SECTIONS: comma separated section names to a bit set */
/* url -o params.  Each base URL as the node lists it, then the
 * parameters the request adds, which are the same for every base.
 * DasSrc_reqUrl() writes the base, then '?' or '&', then the request's
 * parameters, so they are what follows base 0 in sUrl.  Decoded as a
 * server reads a query, '+' is a space, but control characters stay
 * encoded so a value can't break the layout. */
static void printParams(const DasSrc* pSrc, const char* sUrl)
{
	for(size_t u = 0; u < DasSrc_numBaseUrls(pSrc); ++u){
		const char* sBase = DasSrc_baseUrlAt(pSrc, u);
		if(sBase) printf("%s\n", sBase);
	}
	const char* sBase = DasSrc_baseUrlAt(pSrc, 0);
	size_t uBase = sBase ? strlen(sBase) : 0;
	if((sBase == NULL)||(strncmp(sUrl, sBase, uBase) != 0)||(sUrl[uBase] == '\0')) return;

	for(const char* p = sUrl + uBase + 1; *p; ){
		const char* pEnd = strchr(p, '&');
		if(pEnd == NULL) pEnd = p + strlen(p);
		if(pEnd > p){
			fputs("   ", stdout);
			for(const char* c = p; c < pEnd; ++c){
				if(*c == '+'){ putchar(' '); continue; }
				if((*c == '%') && (pEnd - c >= 3) &&
				   isxdigit((unsigned char)c[1]) && isxdigit((unsigned char)c[2])){
					char sHex[3] = {c[1], c[2], '\0'};
					unsigned char b = (unsigned char)strtoul(sHex, NULL, 16);
					if((b >= 0x20) && (b != 0x7F)){ putchar(b); c += 2; continue; }
				}
				putchar(*c);
			}
			putchar('\n');
		}
		p = (*pEnd == '&') ? pEnd + 1 : pEnd;
	}
}

static bool parseShow(const char* sList, unsigned* puShow)
{
	char sCopy[256];
	strcpyz(sCopy, sList, sizeof(sCopy));
	*puShow = 0;
	for(char* pTok = strtok(sCopy, ","); pTok != NULL; pTok = strtok(NULL, ",")){
		das_srcsec sec;
		if(strieq(pTok, "all")) *puShow = SHOW_ALL;
		else if(strieq(pTok, "examples")||strieq(pTok, "example")||strieq(pTok, "ex")) *puShow |= SHOW_EXAMPLES;
		else if(strieq(pTok, "sources")||strieq(pTok, "source")||strieq(pTok, "src")) *puShow |= SHOW_SOURCES;
		else if(secAlias(pTok, &sec)) *puShow |= (1u << sec);
		else return false;
	}
	return true;
}

int main(int argc, char** argv)
{
	daslib_init(PROG, DASERRDISP_EXIT, DASLOG_INFO, NULL);
	daslog_showLine(false);   /* messages here are for users, not debuggers */

	char sAgent[64];
	snprintf(sAgent, sizeof(sAgent), PROG " (das2C %s)", daslib_version());
	DasCat* pCat = NULL;   /* made once the arguments are in, see below */

	const char* sCmd = NULL;
	const char* sPath = NULL;
	const char* sCacheArg = NULL;
	const char* sSeedArg = NULL;
	const char* aMountArg[MAX_MOUNTS];
	size_t uMountArg = 0;
	const char* sAccept = NULL;   /* -t, else the two step default in pickSrc */
	char sAcceptBuf[1024];
	bool bHelp = false;
	bool bRoot = false;
	bool bUpdate = false;
	bool bNoCache = false;
	const char* sOutput = NULL;   /* -o, read once the command is known */
	const char* aKeyVals[MAX_KEYS];   /* KEY=VALUE arguments, applied in order */
	size_t uKeyVals = 0;
	const char* aRaw[MAX_RAW];        /* -p PARAM=VALUE arguments */
	size_t uRaw = 0;
	int nExample = DASSRC_BLANK_REQ;
	float rTimeout = 10.0;

	for(int i = 1; i < argc; ++i){
		const char* sArg = argv[i];
		if(dasarg_isOpt(sArg, "-h", "--help", NULL)){ bHelp = true; continue; }
		if(dasarg_isOpt(sArg, "-v", "--version", NULL)){
			printf(PROG " (das2C %s)\n", daslib_version());
			return RET_OK;
		}
		if(dasarg_isOpt(sArg, "-r", "--root", NULL)){ bRoot = true; continue; }
		if(dasarg_isOpt(sArg, "-u", "--update", NULL)){ bUpdate = true; continue; }
		if(dasarg_isOpt(sArg, NULL, "--no-cache", NULL)){ bNoCache = true; continue; }

		/* Options with a value, given as -x V, --xx V or --xx=V */
		const char* sVal = NULL;
		char cOpt = '\0';
		if     (dasarg_optVal(argv, argc, "-l", "--log=",     &i, &sVal)) cOpt = 'l';
		else if(dasarg_optVal(argv, argc, "-s", "--seed=",    &i, &sVal)) cOpt = 's';
		else if(dasarg_optVal(argv, argc, "-m", "--mount=",   &i, &sVal)) cOpt = 'm';
		else if(dasarg_optVal(argv, argc, "-o", "--output=",  &i, &sVal)) cOpt = 'o';
		else if(dasarg_optVal(argv, argc, "-t", "--type=",    &i, &sVal)) cOpt = 't';
		else if(dasarg_optVal(argv, argc, "-p", "--param=",   &i, &sVal)) cOpt = 'p';
		else if(dasarg_optVal(argv, argc, NULL, "--cache=",   &i, &sVal)) cOpt = 'c';
		else if(dasarg_optVal(argv, argc, NULL, "--timeout=", &i, &sVal)) cOpt = 'w';
		if(cOpt != '\0'){
			if(sVal == NULL){
				fprintf(stderr, PROG ": option %s needs a value, use -h for help\n", sArg);
				return RET_USAGE;
			}
			switch(cOpt){
			case 's': sSeedArg = sVal; break;
			case 'm':
				if(uMountArg >= MAX_MOUNTS){
					fprintf(stderr, PROG ": at most %d mounts\n", MAX_MOUNTS);
					return RET_USAGE;
				}
				aMountArg[uMountArg++] = sVal;
				break;
			case 't':
				if(!expandTypes(sVal, sAcceptBuf, sizeof(sAcceptBuf))){
					fprintf(stderr, PROG ": -t list is too long\n");
					return RET_USAGE;
				}
				sAccept = sAcceptBuf;
				break;
			case 'o': sOutput = sVal; break;
			case 'c': sCacheArg = sVal; break;
			case 'w':{
				char* pEnd = NULL;
				rTimeout = strtof(sVal, &pEnd);
				if((pEnd == sVal)||(*pEnd != '\0')||!(rTimeout > 0)){
					fprintf(stderr, PROG ": --timeout takes a number of seconds, not '%s'\n", sVal);
					return RET_USAGE;
				}
				break;
			}
			case 'l':{
				int nLvl = daslog_levelFromStr(sVal);
				if(nLvl < 0){
					fprintf(stderr, PROG ": -l takes one of critical, error, warning, info, "
					        "debug, trace, not '%s'\n", sVal);
					return RET_USAGE;
				}
				daslog_setLevel(nLvl);
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
	snprintf(sCredFile, sizeof(sCredFile), "%s" DASFS_SEPS CRED_FILE, dasfs_userDir());
	DasCredMgr* pMgr = new_DasCredMgr();
	DasCredMgr_setStore(pMgr, sCredFile);
	DasCredMgr_load(pMgr);   /* no file yet is fine */
	DasCredMgr_setPrompt(pMgr, DasCredMgr_termPrompt, NULL);
	pCat = new_DasCat(pMgr, sAgent);

	/* Where paths start: -r, -s, the environment, else the das2 sites
	   node, found through the das catalog root.  The seed is set before
	   the mounts so a mount can complain about a conflicting one. */
	if(bRoot && (sSeedArg != NULL)){
		fprintf(stderr, PROG ": -r and -s can't be used together, use -h for help\n");
		return finish(pCat, RET_USAGE);
	}
	const char* sFrom = "-s";
	const char* sEnv = getenv(SEED_ENV);
	if(!bRoot && (sSeedArg == NULL) && (sEnv != NULL) && (sEnv[0] != '\0')){
		sSeedArg = sEnv;
		sFrom = SEED_ENV;
	}
	bool bSites = !bRoot && (sSeedArg == NULL);
	if(sSeedArg != NULL){
		if(!applySeedArg(pCat, sSeedArg, sFrom)) return finish(pCat, RET_USAGE);
	}
	else if(bSites){
		DasCat_setSeed(pCat, NULL, DASNODE_URI_SITES);
	}

	sFrom = "-m";
	sEnv = getenv(MOUNT_ENV);
	if((uMountArg == 0) && (sEnv != NULL) && (sEnv[0] != '\0')){
		aMountArg[uMountArg++] = sEnv;
		sFrom = MOUNT_ENV;
	}
	for(size_t u = 0; u < uMountArg; ++u)
		if(!applyMountArg(pCat, aMountArg[u], sFrom)) return finish(pCat, RET_USAGE);

	if(sCmd == NULL){
		fprintf(stderr, PROG ": no command given, use -h for help\n");
		return finish(pCat, RET_USAGE);
	}

	if(strcmp(sCmd, "seeds") == 0){
		size_t uSeeds = 0;
		const char** psSeeds = DasNode_seedUrls(&uSeeds);
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
	if((bJson || bUrl || bData) && (sPath == NULL) && (DasCat_seedUrl(pCat) == NULL)){
		fprintf(stderr, PROG ": the %s command needs a PATH, use -h for help\n", sCmd);
		return finish(pCat, RET_USAGE);
	}
	if(bNode && (uKeyVals > 0))
		daslog_warn("KEY=VALUE arguments are ignored by the node command");
	bool bUrlParams = false;
	if(sOutput != NULL){
		if(bNode){
			if(!parseShow(sOutput, &g_uShow)){
				fprintf(stderr, PROG ": -o for the node command takes a list from examples, "
				        "coords, data, options, formats, sources, all\n");
				return finish(pCat, RET_USAGE);
			}
		}
		else if(bUrl){
			if(strieq(sOutput, "params")) bUrlParams = true;
			else if(!strieq(sOutput, "url")){
				fprintf(stderr, PROG ": -o for the url command is url or params\n");
				return finish(pCat, RET_USAGE);
			}
		}
		else{
			fprintf(stderr, PROG ": -o is for the node and url commands, the %s command "
			        "writes to standard output\n", sCmd);
			return finish(pCat, RET_USAGE);
		}
	}

	/* Node cache: the library writes none unless asked, das_fetch asks for
	   the shared one unless told otherwise.  -u refreshes rather than
	   disables so that the run after a catalog change leaves the cache
	   current.  A user directory too long for the cache only costs speed. */
	if(!bNoCache){
		if((sCacheArg != NULL) && (DasCat_setCacheDir(pCat, sCacheArg) != DAS_OKAY))
			return finish(pCat, RET_USAGE);
		if(bUpdate) DasCat_setCacheAge(pCat, 0);
		DasCat_setCaching(pCat, true);
	}

	/* A fragment on the path is only meaningful for the json command */
	char sPathBuf[1024] = {'\0'};
	const char* sFrag = NULL;
	if(sPath != NULL){
		strcpyz(sPathBuf, sPath, sizeof(sPathBuf));
		char* pHash = strchr(sPathBuf, '#');
		if(pHash != NULL){ *pHash = '\0'; sFrag = pHash + 1; }
		sPath = sPathBuf;
	}

	das_catmiss miss;
	DasNode* pNode = DasCat_resolve(pCat, sPath, &miss);
	if(pNode == NULL){
		explainUnresolved(pCat, &miss, sPath, bSites);
		return finish(pCat, RET_NODE);
	}
	char sShow[1200];
	nodeUri(pNode, miss.sUri, sShow, sizeof(sShow));

	int nRet = RET_OK;

	if(bJson){
		const DasJdo* pJdo = DasNode_jdo(pNode, sFrag);
		if(pJdo == NULL){
			daslog_errorf("Node has no element at '%s'", sFrag ? sFrag : "");
			nRet = RET_NODE;
		}
		else{
			char* sPretty = (char*)DasJdo_newPrettyStr(pJdo, "  ", "\n", NULL);
			if(sPretty){ fputs(sPretty, stdout); fputs("\n", stdout); free(sPretty); }
		}
		return finish(pCat, nRet);
	}

	if(bNode){
		if(DasNode_isCatalog(pNode)){
			/* A source container is one step from the goal.  Users never type
			   the source's name, so by default act as if they had asked for the
			   default source and describe it under the container's URI.  The
			   container listing itself is for -o sources. */
			das_nodetype nType = DasNode_type(pNode);
			bool bSrcSet = (nType == DASNODETYPE_SRCSET)||(nType == DASNODETYPE_COLLECTION);
			bool bSources = (g_uShow & SHOW_SOURCES) != 0;
			if(!bSrcSet || bSources) listCatalog(pNode, stdout, sShow);

			if(bSrcSet){
				DasSrc* pSrc = pickSrc(pCat, pNode, sAccept);
				if(pSrc == NULL){
					/* pickSrc said why; the listing shows what to name */
					if(!bSources) listCatalog(pNode, stdout, sShow);
					nRet = RET_NODE;
				}
				else if(!bSources){
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
			const char* sType = jstr(DasNode_jdo(pNode, NULL), DASNODE_FRAG_TYPE);
			printf("%s:  %s\n%-10s%s\n", sType ? sType : "Node",
			       DasNode_label(pNode) ? DasNode_label(pNode) : "", "URI:", sShow);
			daslog_debugf("%s from %s", sShow, DasNode_srcUrl(pNode));
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
						daslog_errorf("Examples are numbered, there is no '%s'", sArg);
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
	if((nExample < 0) && (uKeyVals == 0) && (uRaw == 0) && (DasSrc_numExamples(pSrc) > 0)){
		nExample = 0;
		daslog_infof("No query given, using example 0, \"%s\"", DasSrc_exampleLblAt(pSrc, 0));
	}

	/* Order matters: the example is the base, explicit keys override it, and
	   -p raw parameters have the last word when the URL is built. */
	DasReq* pReq = new_DasReq_src(pSrc, nExample);
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
		size_t uNeed = DasSrc_reqUrl(pSrc, pReq, 0, sUrl, sizeof(sUrl));
		if(uNeed == 0) nRet = RET_QUERY;
		else if(uNeed >= sizeof(sUrl)){ daslog_error("Query URL too long"); nRet = RET_QUERY; }
		else if(bUrlParams) printParams(pSrc, sUrl);
		else printf("%s\n", sUrl);
		del_DasReq(pReq);
		del_DasSrc(pSrc);
		return finish(pCat, nRet);
	}

	/* data: try each base URL in turn until one answers.  A server that
	   answers with an error is an answer, only connection failures move on.
	   Base 0 is always asked for, so a source with no base URLs at all is
	   reported by the library like any other unusable one. */
	size_t uBases = DasSrc_numBaseUrls(pSrc);
	for(size_t u = 0; ; ++u){
		size_t uNeed = DasSrc_reqUrl(pSrc, pReq, u, sUrl, sizeof(sUrl));
		if(uNeed == 0){ nRet = RET_QUERY; break; }
		if(uNeed >= sizeof(sUrl)){ daslog_error("Query URL too long"); nRet = RET_QUERY; break; }
		nRet = streamUrl(pCat, sUrl, rTimeout);
		if((nRet != RET_SERVER)||(u + 1 >= uBases)) break;
		daslog_warnf("Trying alternate server %s", DasSrc_baseUrlAt(pSrc, u + 1));
	}

	del_DasReq(pReq);
	del_DasSrc(pSrc);
	finish(pCat, nRet);
	daslib_finish();
	return nRet;
}
