#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <das2/core.h>

#define _QDEF(x) #x
#define QDEF(x) _QDEF(x)


#define LOG(x) fprintf(stderr, "LOG: %d\n", (x));
#define EXIT(x) exit(x);

/*
 * output an ascii table that is easily parsed.  Only the first 
 * YScan is output, all other packetDescriptors are ignored.
 *
 */


PktDesc* pdout;
StreamDesc* sdout;

int nitems=-1;
int iplane;
UnitType xunits;

double startT2000;
double endT2000;

int timestr;

void printUsageAndExit() {
    fprintf(stderr, "Usage: toSimpleAsciiTable [--timestr] [--help] [ start end ]\n");
    exit(-1);
}

int parseArgs( int argc, char *argv[] ) {
    int i;
    int argCount=0;
    char *argumentsV[argc];
    char *optionsV[argc];
    int help=0;

    timestr=0;
    
    for (i=0; i<argc; i++ ) {
	if ( argv[i][0]!='-' ) {
	    argumentsV[argCount++]= argv[i];
	} else {
	    optionsV[i-argCount]= argv[i];
        }
    }

    if (argCount!=1 && argCount!=3 ) {
        printUsageAndExit();
	return -1;
    } else {
	if ( argCount==1 ) {
	    startT2000= -1e31;
        } else {
	    startT2000= Units_timeStr2Double( argumentsV[1], UNIT_T2000 );
	    endT2000= Units_timeStr2Double( argumentsV[2], UNIT_T2000 );
        }
	if ( argCount<argc ) {
	    for ( i=0; i<argc-argCount; i++ ) {
		if ( strcmp( optionsV[i], "--timestr" )==0 ) {
		    timestr=1;
		} else if ( strcmp( "--help", optionsV[i] )==0 ) {
                    help=1;
                } else if ( strcmp( "--usage", optionsV[i] )==0 ) {
                    help=1;
                }
            }
        }    

	if ( help ) {
	    printUsageAndExit();
	}
        return 0;
    }
}

void streamDescriptorHandler( StreamDesc* sd, void* ex) {
    printf( "# generated from das2Stream by toSimpleAsciiTable\n" );
}

void packetDescriptorHandler( PktDesc* pd, void* ex) {
    
    PlaneDesc* plane;

    if ( nitems!=-1 ) {
       fprintf( stderr, "Found multiple packets on stream. This is not supported.  Aborting\n" );
       abort();
    }

    iplane=1;
    plane= PktDesc_getPlane( pd, iplane );    

    xunits= PlaneDesc_getUnits( PktDesc_getXPlane(pd) );    
    
    printf( "# yTags: %s\n", PlaneDesc_getYTagsString( plane ) );
    printf( "# first number is x tag, y tags follow\n" );
    if ( ! timestr ) {
	printf( "# x tag is seconds offset\n" );
    }
    nitems= PlaneDesc_getNItems( plane );
    printf( "# nitems=%d\n", nitems );
    
    
}

double* g_buf = NULL;
size_t g_len = 0;

void packetHandler( PktDesc* pd, void* ex  ) {
	int i;
	static double timeBase= -1e31;
	double timeBaseMid;
	double xTag;

	if ( timeBase==-1e31 ) {
		if ( startT2000 != -1e31 ) {
				timeBase= startT2000;
		}
		else {
			timeBase= Units_convertTo( UNIT_T2000, PktDesc_getXTag( pd ), xunits );	
			timeBaseMid= (floor( timeBase / 86400 ) * 86400 ); /* truncate to midnight */	    
			if ( ( timeBase - timeBaseMid ) > 86300 ) {
				timeBaseMid+= 86400.0;
			}
			timeBase= timeBaseMid;
		}
	
		if ( !timestr ) {
			/* The next line leaks memory, maybe Java Programmers should do embedded
				stuff in D, it has a garbage collector */
			printf( "# Time Base: %s\n", Units_double2TimeStr( timeBase, UNIT_T2000 ) );
		}
	}
    
	g_len = PktDesc_getYScan( pd, iplane, &g_buf, g_len);

	xTag= Units_convertTo( UNIT_T2000, PktDesc_getXTag( pd ), xunits );
	if ( startT2000==-1e31 || ( startT2000 <= xTag && xTag < endT2000 ) ) {
		if ( timestr ) {
			/* Again, this call leaks memory */
			printf( "%23s ", Units_double2TimeStr( xTag, UNIT_T2000 ) );
		}
		else {
			printf( "%15f ", xTag - timeBase );
		}
		for ( i=0; i<nitems; i++ ) {
			printf( " %10.3e", g_buf[i] );
		}
	printf( "\n" );
	}
}

void streamExceptionHandler( OobExcept* se, void* ex  ) {
    fprintf( stderr, "stream exception encountered\n" );
    abort();
}

void streamCloseHandler( StreamDesc* sd, void* ex  ) {    
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
    setStreamCloseHandler( sh, streamCloseHandler );

    status= DasIO_processInput( sh, infile );

    return status;
}

