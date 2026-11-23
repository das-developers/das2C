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

/** @file catalog.h A catalog session: the seed, the mounts, credentials
 * and node memory in one object */

#ifndef _das_catalog_h_
#define _das_catalog_h_

#include <das3/node.h>
#include <das3/source.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup catalog Catalogs
 * Finding data sources: the federated das catalog, catalog URIs, and the
 * objects that walk it.
 *
 * A das catalog is a tree of small JSON documents, mostly on web servers,
 * leading from a well known root down to data source definitions.  Each
 * document is a node.  Container nodes (Catalog, SourceSet) list their
 * children by name along with the URLs each child can be loaded from.  Leaf
 * nodes (HttpStreamSrc, FileTimeAgg) describe how to get data.  The tree is
 * federated: the root at das2.org names a few top level trees, and any node
 * may have children maintained by a different organization.  A path is
 * resolved one network request per level, and only the levels on the way to
 * the node asked for are loaded.
 *
 * @par Names
 *
 * A node's URI is composed by walking down from the root: each container
 * appends its separator and the child's name to its URI.  A node does
 * not know its name, the walk gives it one, and the root, which has
 * nothing above it, has none.  The result looks like a file path with a
 * URI scheme in front:
 *
 *   tag:das2.org,2012:site:/uiowa/juno/wav/survey
 *
 * The RFC 4151 "tag" scheme is the usual one, though others such as "doi"
 * work just as well.  URLs are not used as names because they change
 * whenever an organization reshuffles its servers, while a name must not.
 * For that reason a name may not start with a locator scheme, "http",
 * "https", "ftp" and so on; the library reads those as locations, see
 * DasCat_isNodeUrl().
 *
 * Names are strings, and the library does not interpret them: there are
 * no shorthands and no schemes with special meaning.  Paths start at one
 * place, the seed, see below.  With the compiled in seed, the das catalog
 * root, every path is a full name.  A seed given a name lets paths below it be
 * written relative to it: with das.json, the das2 node, as the seed and
 * named tag:das2.org,2012:, "site:/uiowa/juno" is
 * tag:das2.org,2012:site:/uiowa/juno.
 *
 * A name is the node's name and nothing more; it does not end with the
 * separator its children are joined by.  The separator belongs to the
 * node and is read from it: tag:das2.org,2012:site uses ":/", so its
 * child uiowa is tag:das2.org,2012:site:/uiowa, and "site:uiowa" is a
 * different name, one nothing has.  A node with no "separator" uses '/',
 * a das catalog rule.
 *
 * Since names come from the walk, a local file can be declared to be any
 * node.  For testing, the file
 *
 *    ./survey.json
 *
 * can be mounted as
 *
 *    tag:das2.org,2012:site:/uiowa/juno/wav/survey
 *
 * and walking proceeds from there as if the file had been fetched from
 * the catalog, see DasCat_mount() and DasCat_setSeed().  A file loaded
 * with no name at all is a root and has no URI.
 *
 * @par Reading a node
 *
 * Open a catalog session, resolve a path and read the node:
 *
 * @code
 DasCat* pCat = new_DasCat(NULL, "my_program/1.0");

 das_catmiss miss;
 DasNode* pNode = DasCat_resolve(pCat, "tag:das2.org,2012:site:/uiowa/juno/wav/survey", &miss);
 if(pNode == NULL){
    fprintf(stderr, "%s\n", miss.sMsg);   // why, for a person
    ...
 }

 // The query interface of the source, laid out by the das catalog schema
 const DasJdo* pProto = DasNode_jdo(pNode, "protocol");
 ...
 del_DasCat(pCat);   // frees every node the session loaded
 @endcode
 *
 * Programs that want to query a data source rather than inspect its node
 * use new_DasSrc_cat(), which resolves the path and builds a DasSrc, see
 * source.h.
 *
 * Two objects do the work.  DasCat, in das3/catalog.h, is a session: it
 * holds the seed, which is where paths start, any mounts, the credentials
 * and user agent every load uses, and the memory of every node it loads.
 * DasNode, in das3/node.h, is one node: its type, URI, source URL and
 * parsed document, with DasNodeWalk to step down to a child.  Most
 * programs only construct a DasCat.  The node interface is for programs
 * that fetch for themselves, see "Nodes without a session" in node.h.
 *
 * @par Seeds, mounts, and the root
 *
 * The root is the top of the name space, the one node with no URI, from
 * which names are composed starting from nothing.  It always exists, as
 * an idea; a session may just have no way to reach it.
 *
 * A seed is the node a session's memory starts from, with nothing above it
 * in memory.  A session has one.  The compiled in seed is the root of the
 * das catalog, see DasNode_seedUrls(); a session loads it
 * on first use and keeps it, so the tree below it grows as paths are
 * resolved.  Another node of the das catalog can be the seed, named
 * alone; it is found through the das catalog root, so the root's mirrors
 * serve it too.  A file can be the seed instead, given the name of the node it
 * is, or none.  See DasCat_setSeed().  A seed given no name is a root.
 * A seed given a name stands for that node only: the root above it still
 * exists, but nothing in the session leads there.  Lookups below it work
 * just as they would from the root.
 *
 * A mount, see DasCat_mount(), is a node grafted at a name in the session's
 * name space.  It is consulted before the seed for anything under that
 * name, and when one mount is under another the longer name wins.  A
 * mount is named by its full name.  Mounts do not move where paths start,
 * so adding one can't change what an unrelated path means.
 *
 * A name is under a node only when the node's own separator comes next.
 * With the seed doi:12457:tracers.tracers-1, whose separator is ".",
 * doi:12457:tracers.tracers-1.ace is under it but
 * doi:12457:tracers.tracers-10 is another name and outside it.  The same
 * goes for mounts: a name that only starts like a mount's is asked of the
 * next shorter mount, or the seed.  So the seed or mount is loaded to
 * decide, since the separator is in its document.  A source has no
 * separator; for one, a name that goes on with a letter, digit, '_' or
 * '-' is taken to be another node, and anything else a path under it,
 * where there is nothing.
 *
 * Paths start at the seed.  Below a root, the compiled in seed or a file
 * given no name, a path is a name from the root, so with the das catalog
 * root it is a full name.  Below a named seed, a path that starts with the seed's
 * name, or with a mount's, is a full name, compared as text; any other is
 * relative to the seed and joined to its name by the seed's own
 * separator.  A full name for a node elsewhere is then a path below the
 * seed that is not there.  To look below some other node, see
 * DasCat_subNode().
 *
 * All of these may be changed at any time and take effect on the next
 * lookup.  Nodes loaded before a change are not freed, so pointers a
 * program holds stay good until del_DasCat(), but they are no longer used
 * to resolve paths under a changed name.  Changes are expected to be rare;
 * each one leaves its old nodes in memory.
 *
 * @code
 DasCat* pCat = new_DasCat(NULL, "my_program");

 // Start at the das2 sites, so "uiowa/juno" is
 // tag:das2.org,2012:site:/uiowa/juno
 DasCat_setSeed(pCat, NULL, "tag:das2.org,2012:site");

 // A development copy of one branch over the federated catalog
 DasCat_mount(pCat, "https://dev.example.org/uiowa.json", "tag:das2.org,2012:site:/uiowa");

 // ... later, back to the real one
 DasCat_unmount(pCat, "tag:das2.org,2012:site:/uiowa");

 // An offline catalog: a file stands in for site:/uiowa, and paths
 // start at it, so "juno/wav" is tag:das2.org,2012:site:/uiowa/juno/wav
 DasCat_setSeed(pCat, "./uiowa.json", "tag:das2.org,2012:site:/uiowa");
 DasCat_mount(pCat, "./cassini.json", "tag:das2.org,2012:site:/uiowa/cassini");

 // ... and back to the das catalog root
 DasCat_setSeed(pCat, NULL, NULL);
 @endcode
 *
 * @par When a path does not resolve
 *
 * The functions that load nodes, DasCat_resolve(), DasCat_subNode(),
 * DasCat_seed(), new_DasSrc_cat() and DasCat_check(), take a das_catmiss
 * from the caller and fill it in, success or failure.  On failure it says
 * which kind of problem it was, see DASMISS_OUTSIDE and the codes after
 * it, the URI that was tried, the document that would not load, and the
 * deepest container on the path that did load, with what it lists.
 * A program that does not care why gives NULL; the reason is then logged
 * at warning level, since it has nowhere else to go.  A failed lookup is
 * not an error to the library: nothing is raised, whatever error
 * disposition daslib_init() was given.
 */

/** @addtogroup catalog
 * @{
 */

/** Default for DasCat_setCacheAge(), in seconds */
#define DASCAT_NODE_TTL_SEC 3600

/** @name Why a lookup failed
 * Values of das_catmiss.nCause.  Zero, DAS_OKAY, means the lookup worked.
 * @{
 */

/** Outside the session's catalog: the name starts like the named seed's
 * but is not under it, "...tracers-10" for the seed "...tracers-1", and
 * no mount covers it */
#define DASMISS_OUTSIDE     1

/** The container that should list the next name does not, or nothing
 * below the deepest node that loaded continues the path */
#define DASMISS_NOCHILD     2

/** A document could not be fetched or read: an HTTP error, no answer, a
 * missing file, or no mirror of the das catalog root answering.
 * das_catmiss.nHttp has the status. */
#define DASMISS_LOAD        3

/** A document arrived but is not a usable catalog node */
#define DASMISS_PARSE       4

/** The node is listed with a type this library can list but not load */
#define DASMISS_UNSUPPORTED 5

/** new_DasSrc_cat() only: the path ends at a container, not a data source */
#define DASMISS_NOTSRC      6

/** The seed or a mount is named with its own separator on the end,
 * "...site:/" for the node "tag:das2.org,2012:site".  A mistake in the
 * session's setup, not in the path asked for; found when the node loads,
 * since that is when its separator is known. */
#define DASMISS_BADNAME     7

/** @} */

/** Size of the das_catmiss URI and URL buffers */
#define DASCAT_MISS_URI_SZ 512

/** Size of the das_catmiss message buffer */
#define DASCAT_MISS_MSG_SZ 512

/** Why a catalog lookup failed, or what it resolved
 *
 * Filled in by every catalog function that loads nodes, success or not.
 * The caller owns it; it is usually on the stack and holds no pointers
 * that need freeing.  Strings are cut to fit their buffers.
 *
 * @code
 das_catmiss miss;
 DasNode* pNode = DasCat_resolve(pCat, "uiowa/juno/wav/survy", &miss);

 // pNode        NULL
 // miss.nCause  DASMISS_NOCHILD
 // miss.sUri    "tag:das2.org,2012:site:/uiowa/juno/wav/survy"
 // miss.pNear   the wav container
 // miss.sRest   "survy"
 // miss.sMsg    "No child named 'survy' under
 //               tag:das2.org,2012:site:/uiowa/juno/wav.  Children: survey burst"
 @endcode
 *
 * A program that wants more than the message can look at what pNear lists,
 * see DasNode_subNameAt(), or load a child of it with DasCat_subNode().
 */
typedef struct das_catmiss_t {

	/** DAS_OKAY, or one of the DASMISS_ codes */
	int nCause;

	/** For DASMISS_LOAD from an HTTP server, the final status code; -1 if
	 * no server answered, 0 when no request was involved */
	int nHttp;

	/** The deepest container on the path that loaded, owned by the
	 * session, or NULL when none did, on success, or when it was a mount
	 * that failed */
	DasNode* pNear;

	/** The full catalog URI that was resolved or tried, a relative path
	 * joined to the seed's name.  For a path given by location, the
	 * location; for a seed that is a root, "". */
	char sUri[DASCAT_MISS_URI_SZ];

	/** The URL or file of the document that failed to load or parse, ""
	 * when none did */
	char sUrl[DASCAT_MISS_URI_SZ];

	/** The part of the path below pNear, such as "survey/das2", "" when
	 * pNear is NULL */
	char sRest[DASCAT_MISS_URI_SZ];

	/** What went wrong, in a sentence for a person, "" on success */
	char sMsg[DASCAT_MISS_MSG_SZ];

} das_catmiss;

/** @class DasCat catalog.h <das3/catalog.h>
 *
 * A catalog session, opaque.
 *
 * A DasCat turns catalog paths into nodes.  It remembers the seed, the
 * mounts, which credentials and user agent to
 * send, and every node it has loaded, so a program, a MATLAB extension or
 * a Python module can say "resolve this path" and get a node back without
 * managing any of that itself.
 *
 * Paths given to DasCat_resolve() are catalog paths, not web paths, in any
 * of these forms:
 *
 *   - a full catalog name: "tag:das2.org,2012:site:/uiowa/..."
 *   - below a named seed, a path relative to it: "juno/wav/survey"
 *   - a node by location, a URL or a file: "https://host/node.json",
 *     "./node.json".  Such a node is loaded on its own, has no URI, and
 *     das_catmiss.sUri reports the location in its place.
 *
 * Nodes returned by the session are owned by it and live until
 * del_DasCat(), whatever is mounted or unmounted in the meantime.
 *
 * @code
 DasCat* pCat = new_DasCat(NULL, "my_program");
 das_catmiss miss;
 DasNode* pNode = DasCat_resolve(pCat, "tag:das2.org,2012:site:/uiowa/juno/wav/survey", &miss);
 if(pNode == NULL){
    // miss.pNear is the deepest catalog that did load, miss.sRest what
    // it lacks, miss.sMsg says so in words
 }
 del_DasCat(pCat);
 @endcode
 */
typedef struct das_cat_t DasCat;


/** Create a catalog session
 *
 * The session keeps the nodes it loads in memory until del_DasCat() and
 * writes no files.  To keep copies between runs see DasCat_setCaching().
 * The seed is the root of the das catalog, so paths are full names; DasCat_setSeed() can start lower.  Nothing is fetched until the
 * first lookup.
 *
 * @param pMgr Credentials manager consulted when a node or data source asks
 *        for a login.  May be NULL for public access only.  Ownership
 *        passes to the catalog, del_DasCat() frees it.
 * @param sAgent User agent string sent with every request, copied.  NULL
 *        sends the library default.
 * @returns a new catalog, free with del_DasCat().
 * @memberof DasCat
 */
DAS_API DasCat* new_DasCat(DasCredMgr* pMgr, const char* sAgent);

/** Free a catalog session, every node it loaded, and its credentials manager
 * @memberof DasCat
 */
DAS_API void del_DasCat(DasCat* pThis);

/** The credentials manager, borrowed, for data requests made by the caller
 * @returns the manager given to new_DasCat(), or NULL
 * @memberof DasCat
 */
DAS_API DasCredMgr* DasCat_credMgr(const DasCat* pThis);

/** The user agent string, borrowed
 * @returns the string given to new_DasCat() or DasCat_setAgent(), or NULL
 * @memberof DasCat
 */
DAS_API const char* DasCat_agent(const DasCat* pThis);

/** Change the user agent string sent with every request from now on
 *
 * For a binding whose callers name their program after the session was
 * made.  Nodes already loaded are not fetched again.
 *
 * @param pThis The catalog session
 * @param sAgent The new string, copied.  NULL or "" sends the library
 *        default.
 * @memberof DasCat
 */
DAS_API void DasCat_setAgent(DasCat* pThis, const char* sAgent);

/** Turn this session's disk cache of catalog nodes on or off
 *
 * Off in a new session, which writes no files.  Walking a catalog costs
 * one request per level, so a disk cache is what makes the second run of
 * a program fast.  On, nodes fetched over HTTP are kept in the
 * DasCat_setCacheDir() directory and used for DasCat_setCacheAge()
 * seconds without asking the server.  If no directory has been chosen
 * when caching is turned on, the default is: dasfs_userDir() followed by
 * cache/nodes, the spot the das tools share.  The age defaults to
 * DASCAT_NODE_TTL_SEC.  Settings can be made before or after, and survive
 * turning caching off and on again.
 *
 * A copy older than the age is not thrown away: the server is asked
 * whether it changed, using the ETag or Last-Modified it came with, and
 * only a changed node is downloaded again.  A server that cannot be
 * reached leaves the old copy in use, with a warning.
 *
 * Some nodes are not written: those read from local files, those built
 * from bytes a program fetched itself (see new_DasNode_json()), and those
 * the server marked "Cache-Control: no-store".
 *
 * @code
 DasCat* pCat = new_DasCat(NULL, "my_program");
 DasCat_setCaching(pCat, true);                // the shared spot
 // or first: DasCat_setCacheDir(pCat, "/data/nodes");
 @endcode
 *
 * @param pThis The catalog session
 * @param bOn true to keep copies, false to stop
 * @returns DAS_OKAY, or DASERR_CAT, logged not raised, if the default
 *        directory name is too long.  Caching then stays off.
 * @memberof DasCat
 */
DAS_API das_errcode DasCat_setCaching(DasCat* pThis, bool bOn);

/** Is the disk cache on?  See DasCat_setCaching()
 * @memberof DasCat
 */
DAS_API bool DasCat_caching(const DasCat* pThis);

/** Choose where the disk cache keeps node copies; does not turn it on
 *
 * Changing it while caching is on applies from the next fetch; nodes
 * already loaded stay in memory and copies in the old directory are left
 * where they are.
 *
 * @param pThis The catalog session
 * @param sDir The directory, used as given and created on first write.
 *        NULL picks the default now, from the current dasfs_userDir(), see
 *        DasCat_setCaching().
 * @returns DAS_OKAY, or DASERR_CAT if sDir is empty or too long (or the
 *        default is), logged not raised so a program can pick its exit
 *        code for a bad argument.  The setting is then unchanged.
 * @memberof DasCat
 */
DAS_API das_errcode DasCat_setCacheDir(DasCat* pThis, const char* sDir);

/** The directory node copies go in
 *
 * @returns the directory, or NULL until one is chosen, by
 *        DasCat_setCacheDir() or by turning caching on.  Not NULL just
 *        because caching is off, see DasCat_caching().
 * @memberof DasCat
 */
DAS_API const char* DasCat_cacheDir(const DasCat* pThis);

/** How long a cached node is used before the server is asked about it
 *
 * @param pThis The catalog session
 * @param nMaxAgeSec Seconds.  Zero or less means check every time, which
 *        costs one round trip per level and no body bytes when the server
 *        supports validators, and a full fetch when it does not.
 * @memberof DasCat
 */
DAS_API void DasCat_setCacheAge(DasCat* pThis, int nMaxAgeSec);

/** The age setting, see DasCat_setCacheAge()
 * @memberof DasCat
 */
DAS_API int DasCat_cacheAge(const DasCat* pThis);

/** Choose the node paths start from
 *
 * Replaces the seed set earlier, or the compiled in one.  Mounts are
 * kept, before or after, and with a file seed they make up the rest of
 * the session's catalog; this is how an offline catalog is assembled from
 * a few files.
 *
 * The name says which node of the das catalog the seed is.  Given alone,
 * without sUrl, the seed is the node of that name in the das catalog,
 * found by walking down from the compiled in root, so the root's mirrors
 * serve it too.  The walk goes through the root only; mounts do not
 * change which node the seed is.
 *
 * Given with sUrl, the name declares what the file is.  A development server's copy of
 * a catalog branch stands for the branch's federated name, so
 * requisitions made against it are good against the live catalog too.
 * Paths start at the node: relative ones are joined to its name, and full
 * names under it or under a mount resolve as usual.  The root above the
 * node still exists, the session just can't reach it.
 *
 * Without a name the seed is a root: it has no URI, and every path is a
 * name from it.  A copy of the das catalog root is a root seed whose
 * children are named by URI.  A single
 * server's tree is one whose children are not, so the nodes below it are
 * named by their path from it, and requisitions made against sources
 * below it carry those names.  Give the real URI when the file stands in
 * for a catalog node, so that requisitions match it.
 *
 * Nothing is fetched here.  The seed is loaded by the first lookup that
 * needs it, so this fails only for a malformed argument; DasCat_check()
 * loads it at once.
 *
 * @param pThis The catalog session
 * @param sUrl A URL or file path of a node document, or NULL to find
 *        the seed through the compiled in das catalog root.  A root
 *        loaded earlier in the session is used again without fetching it.
 * @param sUri The catalog URI the node has, or NULL for a root.  Taken
 *        as written: a name that ends with the node's own separator is
 *        reported as DASMISS_BADNAME by every lookup once the node loads.
 *        NULL for both is the compiled in root itself, the state a new
 *        session starts in.
 * @returns DAS_OKAY, or DASERR_CAT, logged not raised, if sUrl is not a
 *        location or sUri is a location.
 * @memberof DasCat
 */
DAS_API das_errcode DasCat_setSeed(DasCat* pThis, const char* sUrl, const char* sUri);

/** The location given to DasCat_setSeed(), or NULL when the seed comes
 * from the compiled in das catalog root, see DasNode_seedUrls()
 * @memberof DasCat
 */
DAS_API const char* DasCat_seedUrl(const DasCat* pThis);

/** The name given to DasCat_setSeed(), as given, or "" for a seed that is
 * a root, the compiled in one included.  Paths start here: below a root
 * every path is a full name, below a named seed a path is relative to it
 * unless it starts with this name.  Not NULL; loads nothing.
 * @memberof DasCat
 */
DAS_API const char* DasCat_seedUri(const DasCat* pThis);

/** The seed node, loading it if need be
 *
 * The node chosen with DasCat_setSeed(), or the root of the das catalog,
 * with no URI, whose children are the top level catalogs.  For
 * programs that list the catalog from the top or walk it themselves with
 * DasCat_subNode().
 *
 * @param pThis The catalog session
 * @param pMiss Receives why not, DASMISS_LOAD or DASMISS_PARSE, see
 *        das_catmiss.  NULL logs the reason instead.
 * @returns the node, owned by the session, or NULL
 * @memberof DasCat
 */
DAS_API DasNode* DasCat_seed(DasCat* pThis, das_catmiss* pMiss);

/** Put a node, loaded by location, at a name
 *
 * Paths under the name load from sUrl and shadow whatever the seed, or a
 * mount at a shorter name, says for them; a path the mounted tree does
 * not have fails there.  Everything else resolves as before.  Mounting at
 * a name already mounted replaces the earlier mount.  A mount does not
 * move where paths start.
 *
 * Nothing is fetched here.  The node is loaded by the first lookup of a
 * name that starts with this one, since its separator decides whether
 * the name is under it, see "Seeds, mounts, and the root" above.  A
 * mount that won't load fails those lookups.  A program that mounts and
 * then reads no catalog pays nothing.  DasCat_check() loads every mount
 * at once.
 *
 * May be called at any time.  Nodes already loaded under the name stay in
 * memory and keep their pointers but are no longer used to resolve paths.
 *
 * @param pThis The catalog session
 * @param sUrl Where to load the node from, a URL or a file
 * @param sUri The full name to mount it at, taken as written: below a
 *        named seed it is not joined to the seed's name.  Below a root
 *        file seed whose children have no URI, a full name is a path from
 *        it.  One that ends with the node's own separator is reported as
 *        DASMISS_BADNAME, as for DasCat_setSeed().
 * @returns DAS_OKAY, or DASERR_CAT, logged not raised, if sUrl is not a
 *        location or sUri is NULL, "" or a location.
 * @memberof DasCat
 */
DAS_API das_errcode DasCat_mount(DasCat* pThis, const char* sUrl, const char* sUri);

/** Remove a mount, by name or by location
 *
 * The seed is not a mount and is not touched, see DasCat_setSeed().
 *
 * @param pThis The catalog session
 * @param sUrlOrUri A name given to DasCat_mount(), matched exactly: "site:/uiowa" removes a mount at
 *        site:/uiowa and none below it.  Or a location, which removes
 *        every mount loaded from it.
 * @returns DAS_OKAY, or DASERR_CAT, logged not raised, when sUrlOrUri is
 *        NULL or nothing matched, since unmounting what is not mounted is
 *        a mistake in the program.
 * @memberof DasCat
 */
DAS_API das_errcode DasCat_unmount(DasCat* pThis, const char* sUrlOrUri);

/** Number of mounts; the seed is not one, see DasCat_seedUrl()
 * @memberof DasCat
 */
DAS_API size_t DasCat_numMounts(const DasCat* pThis);

/** Get a mount
 *
 * @param pThis The catalog session
 * @param uIdx Which one, in the order mounted, less than DasCat_numMounts()
 * @param psUrl Receives the location, owned by the session, NULL if uIdx
 *        is out of range.  May be NULL.
 * @param psUri Receives the name as given, owned by the
 *        session, NULL if uIdx is out of range.  May be NULL.
 * @returns true if uIdx named a mount.  The strings are good until the
 *        mount is replaced or removed.
 * @memberof DasCat
 */
DAS_API bool DasCat_mountAt(
	const DasCat* pThis, size_t uIdx, const char** psUrl, const char** psUri
);

/** Load the seed and the top node of every mount now
 *
 * The seed and the mounts are lazy: a wrong location is found out by the
 * first lookup that needs it.  A program that would rather know at once,
 * say right after reading its configuration, calls this.  A program that
 * does no catalog work does not need to.  Nodes loaded here are kept, so
 * the lookups that follow cost nothing extra.
 *
 * @param pThis The catalog session
 * @param pMiss Receives the first problem found, see das_catmiss, or
 *        DAS_OKAY in nCause when there was none.  NULL logs every problem
 *        found instead.
 * @returns how many failed to load, the seed counted as one; 0 when all
 *        are good
 * @memberof DasCat
 */
DAS_API int DasCat_check(DasCat* pThis, das_catmiss* pMiss);

/** Resolve a path to a node
 *
 * Network access happens here, one request per catalog level not yet
 * loaded.  Nodes are kept, a second resolve of the same path costs nothing.
 *
 * @param pThis The catalog session
 * @param sPath A catalog path in any of the forms listed for DasCat.  NULL
 *        or empty is the seed, where paths start.
 * @param pMiss Receives the URI resolved and, on failure, why; see
 *        das_catmiss.  NULL logs the reason for a failure instead.
 * @returns the node, owned by the session, or NULL
 * @memberof DasCat
 */
DAS_API DasNode* DasCat_resolve(DasCat* pThis, const char* sPath, das_catmiss* pMiss);

/** Might this full name be reached with the current seed and mounts?
 *
 * Does not load anything, so it goes by the names alone.  False means
 * nothing in the session has it: sUri does not start with a named seed's
 * name or any mount's.  Given to DasCat_resolve() it would be read as a
 * path below the seed.  True means the seed or a mount would be loaded
 * to find out; the name can still be outside, one that only starts like
 * the seed's, see "Seeds, mounts, and the root", and true says nothing
 * about whether the node is there.
 * @memberof DasCat
 */
DAS_API bool DasCat_covers(const DasCat* pThis, const char* sUri);

/** Load a child of a node, with this session's settings
 *
 * For a program that has resolved a container, listed its children, see
 * DasNode_subNameAt() and DasSrc_loadable(), and picked one.  The child is
 * loaded with the session's credentials, agent and cache, attached under
 * pNode, and lives as long as the tree it is in.  The same as resolving
 * the child's full URI, without composing it.
 *
 * @param pThis The catalog session
 * @param pNode A container node, usually one this session resolved.  A
 *        tree the program loaded itself works too and keeps the child.
 *        NULL is a mistake in the program, logged.
 * @param sRelPath The path below pNode, "das3" or "survey/das2", with or
 *        without the separator pNode uses in front.  Empty means pNode.
 * @param pMiss Receives why not, see das_catmiss; sUri is pNode's URI
 *        followed by sRelPath.  NULL logs the reason instead.
 * @returns the node, or NULL
 * @memberof DasCat
 */
DAS_API DasNode* DasCat_subNode(
	DasCat* pThis, DasNode* pNode, const char* sRelPath, das_catmiss* pMiss
);

/** Resolve a path and build the data source it leads to.
 *
 * Resolves the path as DasCat_resolve() does and hands the node to
 * new_DasSrc_node().  The path has to end at a data source, not a
 * container, of a node type that das2C understands.
 *
 * @see DasSrc_loadable()
 *
 * @param pThis The catalog session
 * @param sPath A catalog path in any form DasCat_resolve() accepts,
 *        including a URL or file name.
 * @param pMiss Receives why not, see das_catmiss.  Beyond the reasons a
 *        resolve can fail, DASMISS_NOTSRC when the path ends at a
 *        container, which sMsg follows with its loadable children, and
 *        DASMISS_UNSUPPORTED when the node is a source of a kind das2C
 *        cannot query.  NULL logs the reason instead.
 * @returns A new source, to be freed with del_DasSrc() and independent
 *        of this session afterwards, or NULL.
 * @memberof DasCat
 */
DAS_API DasSrc* new_DasSrc_cat(DasCat* pThis, const char* sPath, das_catmiss* pMiss);

/** Is this string a node location rather than a catalog path?
 *
 * True for http://, https:// and file:// URLs, for anything starting with
 * '.', '/', '\' or a drive letter, and for anything ending in ".json".  All else
 * is taken to be a catalog path or URI.  This is the rule DasCat_resolve()
 * uses, published so command line parsers can agree with it.
 *
 */
DAS_API bool DasCat_isNodeUrl(const char* sPath);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_catalog_h_ */
