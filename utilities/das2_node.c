/* This small utility is mostly meant to demonstrate an interface and thus
 * has a liberal open source license...
 *
 * The MIT License
 * 
 * Copyright 2023 Chris Piker
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
 * copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>

#include <das2/core.h>

#define PROG_ERR 64

#define AGENT "das2C"

/* ************************************************************************* */
void prnHelp()
{
	fprintf(stderr,
"SYNOPSIS\n"
"   das2_node - Read a node out of the federated catalog\n"
"\n"
"USAGE\n"
"   das2_node [-h] [-R] [-a ALT_ROOT] [TAG_URI]\n"
"\n"
"DESCRIPTION\n"
"   das2_node is a small utility which resolves a catalog URI to URL and then\n"
"   writes the named catalog node to standard output. By default the\n"
"   builtin root nodes are loaded first, then the catalog is walked to find the\n"
"   requested sub-node. The walking algorithm automatically backs-up and tries\n"
"   alternate branches when URL resolution fails.  If there is only one URL for\n"
"   a given branch, or if walking all branches still fails to load the node then\n"
"   resolution fails.\n"
"\n"
"   Any node of type Catalog may be a used as the root node.  To do so provide\n"
"   and absolute URL to the root in the optional second argument ALT_ROOT_URL\n"
"\n"
"OPTIONS\n"
"\n"
"   -h,--help\n"
"         Print this help text\n"
"\n"
"   -R,--roots\n"
"         Print the builtin root URLs and exit\n"
"\n"
"   -a URL,--alt-root URL\n"
"         Don't use the compiled in root URLs, look for the given object under\n"
"         this alternate root catalog object.  Useful for testing detached\n"
"         catalogs.\n"
"\n"
"   -l,--level\n"
"         The logging level, one of 'none','crit', 'error', 'warn', 'info', \n"
"         'debug', or 'trace'.\n"
"\n"
"\n"
"EXAMPLES\n"
"   Print the compiled in default federated catalog roots:\n"
"      das2_node -R\n"
"\n"
"   Get the U. Iowa Juno site data source catalog:\n"
"      das2_node tag:das2.org,2012:site:/uiowa/juno\n"
"\n"
"   Retrieve a HttpStreamSrc node for Juno Waves Survey data given an\n"
"   explicit URL for the root node:\n"
"      das2_node -a https://das2.org/catalog/das/site/uiowa.json juno/wav/survey/das2\n"
"\n"
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n");
}
		
/* ************************************************************************* */

int main(int argc, char** argv)
{
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	const char* sRootUrl = NULL;
	const char* sNodeUri = NULL;
	size_t uRoots = 0;
	const char** psRoots;

	for(int i = 1; i < argc; i++){
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ){
			prnHelp();
			return 0;
		}
		
		if(strcmp(argv[i], "-R") == 0 || strcmp(argv[i], "--roots") == 0 ){
			psRoots = das_root_urls(&uRoots);
			fprintf(stdout, "Compiled in das federated catalog URLs:\n");
			for(size_t u = 0; u < uRoots; ++u)
				fprintf(stdout, "   %s\n", psRoots[u]);
			return 0;
		}

		if(strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--level") == 0){
			if(argc <= i){
				daslog_error("Logging level argument missing, use -h for help");
				return 4;
			}
			++i;
			daslog_setlevel(daslog_strlevel(argv[i]));
			continue;
		}

		if(strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--alt-root") == 0){
			if(argc <= i){
				daslog_error(
					"Alternate root URL missing after -a or --alt-root, use -h for help."
				);
				return 5;
			}
			++i;
			sRootUrl = argv[i];
			continue;
		}
		
		if(sNodeUri == NULL){
			sNodeUri = argv[i];
			continue;
		}
			
		daslog_error_v("Unknown extra parameter: %s\n", argv[i]);
		return 3;
	}
	
	DasNode* pRoot;
	if(sRootUrl)
		pRoot = new_RootNode_url(sRootUrl, NULL, NULL, AGENT);
	else
		pRoot = new_RootNode(NULL, NULL, AGENT);

	if(pRoot == NULL){
		fprintf(stderr, "ERROR: Couldn't get the root node");
		return 4;
	}

	DasNode* pNode;
	if(sNodeUri) 
		pNode = DasNode_subNode(pRoot, sNodeUri, NULL, AGENT);
	else
		pNode = pRoot;
	
	if(pNode != NULL){
		fprintf(stdout, 
			"Loaded node: %s\n"
			"From URL:    %s\n",
			DasNode_name(pNode), DasNode_srcUrl(pNode)
		);
	}
	else{
		daslog_error_v("Couldn't load %s starting from %s", sNodeUri, DasNode_srcUrl(pRoot));
		return 7;
	}
	
	
	if(DasNode_isJson(pNode)){
		const DasJdo* pJdo = DasNode_getJdo(pNode, NULL);
		const char* pPretty = (char*) DasJdo_writePretty(pJdo, "  ", "\n", NULL);

		fprintf(stdout, "\nIt has the following content:\n%s\n", pPretty);
	}
	else{
		fprintf(stdout, 
			"The object was type %d, there's no printer for it yet.\n",
			pRoot->nType
		);
	}
	
	del_RootNode(pRoot);
	
	return 0;
}
