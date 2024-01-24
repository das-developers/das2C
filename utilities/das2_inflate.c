#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>

#include <das2/core.h>

#define _QDEF(x) #x
#define QDEF(x) _QDEF(x)


#define LOG(x) fprintf(stderr, "LOG: %d\n", (x));
#define EXIT(x) exit(x);

/*
 *
 */


PktDesc pdout[100];
StreamDesc sdout;

int parseArgs( int argc, char *argv[] ) {
    if (argc!=1) {
        fprintf(stderr, "nop simply reads in and outputs the same stream, possibly decompressing it.\n");
        fprintf(stderr, "Usage: nop \n");
        return -1;
    } else {
        return 0;
    }
}

void streamDescriptorHandler( StreamDesc sd ) {

    sdout= cloneStreamDescriptor( createStreamInputDescriptor(stdout), sd );
    sdout->compression="none";
    DasIO_writeStreamDesc( sdout );

}

void packetDescriptorHandler( PktDesc pd ) {

    int i;
    PlaneDesc plane;

    if ( pdout[pd->id] != NULL ) {
        StreamDesc_freeDesc( sdout, pdout[pd->id] );
    }

    pdout[pd->id]= StreamDesc_createPktDesc( sdout, PlaneDesc_getEncoding( PktDesc_getXPlane(pd) ), PlaneDesc_getUnits( PktDesc_getXPlane(pd)) );

    Desc_copyProperties( (Descriptor)pdout[pd->id], (Descriptor)pd );

    for ( i=0; i<pd->uPlanes; i++ ) {
        plane= PktDesc_getPlane( pd, i );
        if ( plane->planeType==PLANETYPE_X ) {
        } else if ( plane->planeType==PLANETYPE_YSCAN ) {
            PktDesc_addPlaneYScan_str(
              pdout[pd->id],
              PlaneDesc_getNItems( plane ),
              PlaneDesc_getYTagsString( plane ),
              PlaneDesc_getYTagUnits( plane ),
              PlaneDesc_getEncoding( plane ),
              PlaneDesc_getUnits( plane ),
              PlaneDesc_getName( plane ) );
        } else if ( plane->planeType==PLANETYPE_Y ) {
            PktDesc_addPlaneY( pdout[pd->id], PlaneDesc_getEncoding( plane ), PlaneDesc_getUnits( plane ), PlaneDesc_getName( plane ) );
        }
    }

    DasIO_writePktDesc( sdout, pdout[pd->id] );

}

void packetHandler( PktDesc pd ) {
    int i;
    double d;

    PktDesc_setXTag( pdout[pd->id], PktDesc_getXTag( pd ) );

    for ( i=1; i<pd->uPlanes; i++ ) {
        if ( PLANETYPE_X==pd->planes[i]->planeType ) {
        } else if ( PLANETYPE_YSCAN==pd->planes[i]->planeType ) {
            PktDesc_setYScan( pdout[pd->id], i, PktDesc_getYScan( pd, i ) );
        } else if ( PLANETYPE_Y==pd->planes[i]->planeType ) {
            d=  getDataPacketYDouble( pd, i );
            setDataPacketYDouble( pdout[pd->id], i, getDataPacketYDouble( pd, i ) );
        }
    }

    DasIO_writePktData( sdout, pdout[pd->id] );
}

void streamExceptionHandler( OobExcept se ) {
    DasIO_writeException( sdout, se );
}

void streamCommentHandler( OobComment sc ) {
    DasIO_writeComment( sdout, sc );
}

void streamCloseHandler( StreamDesc sd ) {
    closeStream(sdout);
}

int main( int argc, char *argv[]) {

    int status;
    StreamHandler sh=NULL;
	 
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
    setStreamCommentHandler( sh, streamCommentHandler );
    setStreamCloseHandler( sh, streamCloseHandler );

    status= DasIO_processInput( sh, infile );

    exit(0);

}

