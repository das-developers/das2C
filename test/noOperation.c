#include <stdio.h>
#include <stdlib.h>

#include <das2stream/Das2Stream.h>

#define LOG(x) fprintf(stderr, "LOG: %d\n", (x));
#define EXIT(x) exit(x);

/*
 *
 */

StreamDescriptor sdout;
PacketDescriptor pdout;

int checkArgs( int argc, char *argv[] ) {
    if (argc!=1) {
        fprintf(stderr, "noOperation--relays the stream w/o processing.  Tests read and write.\n");        
        fprintf(stderr, "Usage: | noOperation > \n");
        return -1;
    } else {
        return 0;
    }
}

void streamDescriptorHandler( StreamDescriptor sd ) {
    sdout= cloneStreamDescriptor( createStreamInputDescriptor(stdout), sd );
    sendStreamDescriptor( sdout );

}

void packetDescriptorHandler( PacketDescriptor pd ) {
    pdout= clonePacketDescriptor( sdout, pd );
    copyProperties( (Descriptor)pdout, (Descriptor)pd );
    sendPacketDescriptor( sdout, pdout );
}

void packetHandler( PacketDescriptor pd ) {    
    int iplane;
    if ( pdout->id != pd->id ) {
        fprintf( stderr, "implementation error\n" );
        abort();
    }
    pdout->dataPacket= pd->dataPacket;
    for ( iplane=0; iplane<pd->nPlanes; iplane++ ) pdout->planes[iplane]->planeDataValid=1;

    sendPacket( sdout, pdout );
}

void streamExceptionHandler( StreamException se ) {
    sendStreamException( sdout, se );
}

void streamCommentHandler( StreamComment sc ) {
    sendStreamComment( sdout, sc );
}

void streamCloseHandler( StreamDescriptor sd ) {
    closeStream(sdout);
}

int main( int argc, char *argv[]) {

    int status;
    StreamHandler sh=NULL;

    FILE * infile;

    if ((status= checkArgs(argc, argv) )!=0) {
        exit(status);
    }

    infile= stdin;

    sh= createStreamHandler(sh);
    setStreamDescriptorHandler( sh, streamDescriptorHandler );
    setPacketDescriptorHandler( sh, packetDescriptorHandler );
    setPacketHandler( sh, packetHandler );
    setStreamExceptionHandler( sh, streamExceptionHandler );
    setStreamCommentHandler( sh, streamCommentHandler );
    setStreamCloseHandler( sh, streamCloseHandler );

    status= processStream( sh, infile );

    exit(0);

}

