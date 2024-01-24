#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>

#include <das2/core.h>


#define LOG(x) fprintf(stderr, "LOG: %d\n", (x));
#define EXIT(x) exit(x);

/*
 *  average the das2Stream on stdin to a lower resolution.  An additional data plane
 *  is added, "peaks" that contains the highest observed value in each bin.
 *
 */


PktDesc* pdout[100];
StreamDesc* sdout;

int parseArgs( int argc, char *argv[] ) {
    if (argc!=1) {
        fprintf(stderr, "Usage: toBinary \n");
        return -1;
    } else {
        return 0;
    }
}

void streamDescriptorHandler( StreamDesc* sd, void* ex ) {

    sdout = createOutputStreamDescriptor(stdout, sd);
    sdout->compression="none";
    DasIO_writeStreamDesc( sdout );

}

void packetDescriptorHandler( PktDesc* pd, void* ex ) {
	int i;
	DasEncoding outDataType;

	if ( pdout[pd->id] != NULL ) {
		StreamDesc_freeDesc( sdout, pdout[pd->id] );
	}
    
	pdout[pd->id]= StreamDesc_clonePktDesc( sdout, pd );
	for ( i=0; i<pdout[pd->id]->uPlanes; i++ ) {
		  
		switch(pd->planes[i]->pEncoding){
		
		case DATATYPE_TIME28:
		case DATATYPE_TIME25:
		case DATATYPE_ASCII24:
			outDataType = DATATYPE_DOUBLE; /* Native Endian Double */
			break;
			
		case DATATYPE_ASCII10:
		case DATATYPE_ASCII14:
			outDataType = DATATYPE_FLOAT;  /* Native Endian Float */
			break;
		
		default:
			outDataType = pd->planes[i]->pEncoding;
		}
		  
		pdout[pd->id]->planes[i]->pEncoding= outDataType;
	}

	PktDesc_revalidate( pdout[pd->id] );
	DasIO_writePktDesc( sdout, pdout[pd->id] );

}

void packetHandler( PktDesc* pd, void* ex ) {
	int i;
	double d;
	double* buf = NULL;
	size_t len = 0;

	PktDesc_setXTag( pdout[pd->id], PktDesc_getXTag( pd ) );

	for(i=0; i<pd->uPlanes; i++ ){
		 
		switch(pd->planes[i]->planeType){
		case PLANETYPE_X:
			/* Skip it, we exlicitly output this first */
			break;
		case PLANETYPE_Y:
		case PLANETYPE_Z:
			d = PktDesc_getValue(pd, i);
			PktDesc_setValue(pdout[pd->id], i, d);
			break;
		
		case PLANETYPE_YSCAN:
			PktDesc_getYScan( pd, i, &buf, len);
			PktDesc_setYScan( pdout[pd->id], i, buf);
			break;
			
		default:
			das2_error(13, "Never heard of plane type %d", pd->planes[i]->planeType);
			break;
		}
 	}

	DasIO_writePktData( sdout, pdout[pd->id] );
	if(buf != NULL) free(buf);
}

void streamExceptionHandler( OobExcept* se, void* ex ) {
    DasIO_writeException( sdout, se );
}

void streamCommentHandler( OobComment* sc, void* ex ) {
    DasIO_writeComment( sdout, sc );
}

void streamCloseHandler( StreamDesc* sd, void* ex ) {
    closeStream(sdout);
}

int main( int argc, char *argv[]) {

    int status;
    StreamHandler* sh=NULL;

    FILE * infile;

	/* Exit on errors, log info messages and above */
	das2_init(argv[0], DAS2_ERRDIS_EXIT, 0, DAS_LL_INFO, NULL);
	 
    if ((status= parseArgs(argc, argv) )!=0) {
        exit(status);
    }

    infile= stdin;

    sh= StreamHandler_init(sh);
    setStreamDescriptorHandler( sh, streamDescriptorHandler );
    setPacketDescriptorHandler( sh, packetDescriptorHandler );
    setPacketHandler( sh, packetHandler );
    setStreamExceptionHandler( sh, streamExceptionHandler );
    setStreamCommentHandler( sh, streamCommentHandler );    
    setStreamCloseHandler( sh, streamCloseHandler );

    status= DasIO_processInput( sh, infile );

	return status;
}

