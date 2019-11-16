#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>

#include <das2/core.h>


#define LOG(x) fprintf(stderr, "LOG: %d\n", (x));
#define EXIT(x) exit(x);

PktDesc* pdout[100];
StreamDesc* sdout;

int parseArgs( int argc, char *argv[] ) {
    if (argc!=1) {
        fprintf(stderr, "deflateStream compresses the input stream.\n");
        fprintf(stderr, "Usage: deflateStream \n");
        return -1;
    } else {
        return 0;
    }
}

void streamDescriptorHandler( StreamDesc* sd, void* ex ) {

    sdout= createOutputStreamDescriptor(stdout, sd);
    sdout->compression= "deflate";
    DasIO_writeStreamDesc( sdout );

}

void packetDescriptorHandler( PktDesc* pd, void* ex  ) {

	int i;
	PlaneDesc* plane;

	if ( pdout[pd->id] != NULL ) {
		StreamDesc_freePktDesc( sdout, pdout[pd->id] );
	}

	pdout[pd->id]= StreamDesc_createPktDesc( sdout, PlaneDesc_getEncoding( PktDesc_getXPlane(pd) ), PlaneDesc_getUnits( PktDesc_getXPlane(pd)) );

	Desc_copyProperties( (Descriptor*)pdout[pd->id], (Descriptor*)pd );

	for ( i=0; i<pd->uPlanes; i++ ) {
		plane= PktDesc_getPlane( pd, i );
		switch(plane->planeType){
		case PLANETYPE_X:
			/* pass, handled above */
			break;
		case PLANETYPE_YSCAN:
			PktDesc_addPlaneYScan_str(
			  pdout[pd->id], PlaneDesc_getNItems( plane ), PlaneDesc_getYTagsString( plane ),
			  PlaneDesc_getYTagUnits( plane ), PlaneDesc_getEncoding( plane ),
			  PlaneDesc_getUnits( plane ),
			  PlaneDesc_getName( plane ) 
			);
			break;

		case PLANETYPE_Y:
			PktDesc_addPlaneY( pdout[pd->id], PlaneDesc_getEncoding( plane ), PlaneDesc_getUnits( plane ), PlaneDesc_getName( plane ) );
			break;
		case PLANETYPE_Z:
			das2_error(13, "<Z> plane handling is not yet implemented.\n");
			break;
		default:
			das2_error(13, "Never heard of plane type %d", pd->planes[i]->planeType);
			break;
		}
	}

	DasIO_writePktDesc( sdout, pdout[pd->id] );
}

void packetHandler( PktDesc* pd, void* ex  ) {
	int i;
	double d;
	double* buf = NULL;
	size_t len = 0;

	for (i=0; i<pd->uPlanes; i++ ) {
		switch(pd->planes[i]->planeType){
		case PLANETYPE_X:
		case PLANETYPE_Y:
		case PLANETYPE_Z:
			d = PktDesc_getValue( pd, i );
			PktDesc_setValue( pdout[pd->id], i, d);
			break;
		case PLANETYPE_YSCAN:
			len = PktDesc_getYScan( pd, i, &buf, len);
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

void streamExceptionHandler( OobExcept* se, void* ex  ) {
    DasIO_writeException( sdout, se );
}


void streamCommentHandler( OobComment* sc, void* ex  ) {
    DasIO_writeComment( sdout, sc );
}

void streamCloseHandler( StreamDesc* sd, void* ex  ) {
    closeStream(sdout);
}

int main( int argc, char *argv[]) {

    int status;
    StreamHandler* sh=NULL;
	 
	/* Exit on errors, log info messages and above */
	das2_init(argv[0], DAS2_ERRDIS_EXIT, 0, DAS_LL_INFO, NULL);
	
    FILE * infile;

    if ((status= parseArgs(argc, argv) )!=0) {
        exit(status);
    }

    infile= stdin;

    sh= StreamHandler_init(sh);
    setStreamDescriptorHandler( sh, streamDescriptorHandler );
    setPacketDescriptorHandler( sh, packetDescriptorHandler );
    setPacketHandler( sh, packetHandler );
    setStreamExceptionHandler( sh, streamExceptionHandler );
    setStreamCloseHandler( sh, streamCloseHandler );
    setStreamCommentHandler( sh, streamCommentHandler );

    status= DasIO_processInput( sh, infile );
	return status; 
}

