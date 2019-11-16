#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <time.h>
#include <sys/timeb.h>
#include <stdlib.h>

#include <das2/core.h>


long bytesReceived;
long reportEvery=100000;
long countReportEvery;

int bytesPerPacket[100];
long startTime;
long lastTime;
struct timeb tp;


int parseArgs( int argc, char *argv[] ) {
    if (argc!=1) {
        fprintf(stderr, "Usage: transferRate \n");
        return -1;
    } else {
        return 0;
    }
}

void streamDescriptorHandler( StreamDesc sd ) {
    bytesReceived=0;
    countReportEvery=0;
    ftime(&tp);
    startTime= ( (long)tp.time-1073706472 ) * 1000 + (long) tp.millitm;
}

void printRate() {
    long nowTime;
    long dt;
    ftime(&tp);
    nowTime= ( (long)tp.time-1073706472 ) * 1000 + (long) tp.millitm;
    dt= nowTime - startTime;
    if ( dt > 0 ) {
        fprintf( stdout, "%16ld bytes %16.3f s %16.1f kB/s \n", bytesReceived, dt/1000.,( (float)bytesReceived ) / dt );
    }
    lastTime= nowTime;
}

void packetDescriptorHandler( PktDesc pd ) {
    bytesPerPacket[pd->id]= pd->uRecBytes;
    printRate();
}

void packetHandler( PktDesc pd ) {
    bytesReceived+= bytesPerPacket[pd->id];
    countReportEvery+= bytesPerPacket[pd->id];
    if ( countReportEvery> reportEvery ) {
       printRate();
       countReportEvery=0;
    }
}


void streamCloseHandler( StreamDesc sd ) {
    printRate();
}


int main( int argc, char *argv[]) {
    int status;
    StreamHandler sh=NULL;
	 
    /* Exit on errors, log info messages and above */
    das2_init(argv[0], DAS2_ERRDIS_EXIT, 0, DAS_LL_INFO, NULL);

    FILE * infile;
    FILE * outfile;

    if ((status= parseArgs(argc, argv) )!=0) {
        exit(status);
    }

    infile= stdin;
    outfile= stdout;

    sh= StreamHandler_init(sh);
    setStreamDescriptorHandler( sh, streamDescriptorHandler );
    setPacketDescriptorHandler( sh, packetDescriptorHandler );
    setPacketHandler( sh, packetHandler );
    setStreamCloseHandler( sh, streamCloseHandler );

    status= DasIO_processInput( sh, infile );

    exit(0);

}

