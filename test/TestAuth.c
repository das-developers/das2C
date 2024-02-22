/** @file TestAuth.c Unit test for server authentication */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 * 
 * This file contains test and example code and is meant to explain an
 * interface.
 * 
 * As United States courts have ruled that interfaces cannot be copyrighted,
 * the code in this individual source file, TestBuilder.c, is placed into the
 * public domain and may be displayed, incorporated or otherwise re-used without
 * restriction.  It is offered to the public without any without any warranty
 * including even the implied warranty of merchantability or fitness for a
 * particular purpose. 
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>

#include <das2/core.h>

#define PROG_ERR 64

/* ************************************************************************* */
void sim_plot_1d(const DasDs* pDs)
{
	/* The one and only coordinate dimension */
	const DasDim* pDimX = DasDs_getDimByIdx(pDs, 0, DASDIM_COORD);
	
	/* Just take the first data dimension, whatever it is */
	const DasDim* pDimY = DasDs_getDimByIdx(pDs, 0, DASDIM_DATA);
	
	const DasVar* pVarX = DasDim_getPointVar(pDimX);
	const DasVar* pVarY = DasDim_getPointVar(pDimY);
	
	/* VERY IMPORTANT POINT: The rank of a dataset is the length of it's index
	 * array only.  This length may have NO DIRECT CORRELATION to the number of
	 * physical dimensions.  Don't assume that these data are rank 1.  For
	 * example waveforms are often recorded as a rank 2 array with the major
	 * index corresponding to the start time of a capture and the minor index
	 * corresponding to the offset of the capture. 
	 * 
	 * Using a das dataset iterator takes care of these issues
	 */
	
	dasds_iterator iter;
	das_datum pair[2];
	char sBufX[128] = {'\0'};
	char sBufY[128] = {'\0'};
	
	for(dasds_iter_init(&iter, pDs); !iter.done; dasds_iter_next(&iter))
	{
		DasVar_get(pVarX, iter.index, pair);
		DasVar_get(pVarY, iter.index, pair + 1);
		
		das_datum_toStr(pair, sBufX, 128, 3);
		das_datum_toStr(pair + 1, sBufY, 128, 3);
		
		printf( "index: ");
		for(int i = 0; i < iter.rank; ++i) printf(" %zd", iter.index[i]);
		printf( " : %s %s\n", sBufX, sBufY);
	}
}


void sim_plot_2d(const DasDs* pDs)
{
	const DasDim* pDimX = DasDs_getDimByIdx(pDs, 0, DASDIM_COORD);
	const DasDim* pDimY = DasDs_getDimByIdx(pDs, 1, DASDIM_COORD);
	const DasDim* pTmp = NULL;
	
	/* Put time or longitude on bottom if they are present */
	if((strcmp("time", DasDim_id(pDimY)) == 0)||
		(strcmp("longitude", DasDim_id(pDimY)) == 0)){
		pTmp = pDimX; pDimX = pDimY; pDimY = pTmp;
	}
	
	/* Just pick the first one */
	const DasDim* pDimZ = DasDs_getDimByIdx(pDs, 0, DASDIM_DATA);
	
	/* Get center points for each dimension, skip width's std_dev or what
	 * ever else might be present */
	const DasVar* pVarX = DasDim_getPointVar(pDimX);
	const DasVar* pVarY = DasDim_getPointVar(pDimY);
	const DasVar* pVarZ = DasDim_getPointVar(pDimZ);

	/* VERY IMPORTANT POINT: The rank fo a dataset is the length of it's
	 * iteration index array and has nothing to do with the number of 
	 * physical dimenions over which it varies.  Don't assume these dat are
	 * rank 2!  For example the MARSIS magnetic field measurements are defined 
	 * in latitude, longitude, and altitude but are only rank 1. */
	
	dasds_iterator iter;
	das_datum set[3];
	char sBufX[128] = {'\0'};
	char sBufY[128] = {'\0'};
	char sBufZ[128] = {'\0'};
	
	for(dasds_iter_init(&iter, pDs); !iter.done; dasds_iter_next(&iter))
	{	
		DasVar_get(pVarX, iter.index, set);
		DasVar_get(pVarY, iter.index, set + 1);
		DasVar_get(pVarZ, iter.index, set + 2);

		das_datum_toStr(set, sBufX, 128, 3);
		das_datum_toStr(set + 1, sBufY, 128, 3);
		das_datum_toStr(set + 2, sBufZ, 128, 3);
		
		printf( "index: ");
		for(int i = 0; i < iter.rank; ++i) printf(" %zd", iter.index[i]);
		printf( " : %s %s %s \n", sBufX, sBufY, sBufZ);
	}
}

void sim_plot_3d(const DasDs* pDs){
	/* Could just reapeat the pattern used for the previous two functions but a
	 * more interesting thing to do would be slicing. (or a boolean condition
	 *  map) Going to punt that for now...
	 */
}


/* ************************************************************************* */
int main(int argc, char** argv)
{
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	const char* sUrl = "https://jupiter.physics.uiowa.edu/das/server?"
			             "server=dataset&dataset=Juno/WAV/Survey&"
			              "start_time=2017-01-01T00:42&end_time=2017-01-01T00:43";
	
	/* Create an credentials manager to handle authenticaion.  We're not going
	 * to cache credentials to disk so the filename argument is null */
	DasCredMngr* pCred = new_CredMngr(NULL);
	
	DasHttpResp res; /* A structure to hold the read session info */
	
	/* Get a connection to the remote web server handling any authentication 
	 * prompts, redirects, and SSL socket setup, as required */
	printf("INFO: Contacting remote HTTP URL %s\n\n", sUrl);
	
	if(!das_http_getBody(sUrl, NULL, pCred, &res, DASHTTP_TO_MIN)){
		printf("ERROR: Could not get body for URL, reason: %s\n", res.sError);
		return PROG_ERR;
	}
	
	/* Make an I/O object to handle reading the stream. Reading from HTTPS
	 * streams requires an SSL reader instead of a plain socket reader */
	DasIO* pIn = NULL;
	if(DasHttpResp_useSsl(&res))
		pIn = new_DasIO_ssl("TestAuth", res.pSsl, "r");
	else
		pIn = new_DasIO_socket("TestAuth", res.nSockFd, "r");
	
	/* Create a stream processor to build datasets from the packet stream */
	DasDsBldr* pBldr = new_DasDsBldr();
	
	/* Add the dataset builder to the stream parser */
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
	
	/* We could add other stream processors if desired such as a progress
	 * monitor, or our own custom stream processor.  Stream processors are
	 * called in the order that they are added to the IO object */
	
	/* Read everything calling our stream processors */
	if(DasIO_readAll(pIn) != 0){
		printf("ERROR: Test failed, couldn't process %s\n", sUrl);
		return PROG_ERR;
	}
	
	/* Get the datasets constructed by the builder */
	size_t uDs = 0;
	DasDs** lDs = DasDsBldr_getDataSets(pBldr, &uDs);
	
	/* Just to have something to do print information about each dataset
	 * and loop over all the data as if binning a spectrogram for
	 * display */
	char sInfo[4096] = {'\0'}; /* dataset info output is verbose, use big buf */
	size_t uCoords = 0;
	for(size_t u = 0; u < uDs; ++u){
		DasDs_toStr(lDs[u], sInfo, 4095);
		fputs(sInfo, stdout);
		printf("Data follow...\n");
		printf("------------------------------------------\n");
		uCoords = DasDs_numDims(lDs[u], DASDIM_COORD);
		switch(uCoords){
			case 1: sim_plot_1d(lDs[u]); break;
			case 2: sim_plot_2d(lDs[u]); break;
			case 3: sim_plot_3d(lDs[u]); break;
			default:
				fprintf(stderr, "Skipping sim-plot of dataset %s as it's defined "
					"in %zu coordinates, could ask the user what coordintes they "
					"want to plot in if this was a real app and not a unittest.",
					DasDs_id(lDs[u]), uCoords);
				break;
		}
		printf("------------------------------------------\n");
		
	}
	fputs("\n", stdout);
	return 0;
}
