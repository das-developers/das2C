/*
 * File:   socketDemo.c
 * Author: jbf
 *
 * Created on January 9, 2004, 9:12 PM
 */

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

void printUsage() {
    fprintf( stderr, "usage: $0 <client|server>\n" );
}

void error(char *msg)
{
    perror(msg);
    exit(0);
}

void beClient() {
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];

    portno = 7800;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    server = gethostbyname("localhost");
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);

    if (connect(sockfd,&serv_addr,sizeof(serv_addr)) < 0)
        error("ERROR connecting");

    strcpy( buffer, "hi there\n" );
    n = write(sockfd,buffer,strlen(buffer));
    if (n < 0)
         error("ERROR writing to socket");

}

void beServer() {
     int sockfd, newsockfd, portno, clilen;
     char buffer[256];
     struct sockaddr_in serv_addr, cli_addr;
     int n;

     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd < 0)
        error("ERROR opening socket");

     bzero((char *) &serv_addr, sizeof(serv_addr));
     portno = 7800;
     fprintf(stderr,"using port 7800\n");
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);
     if (bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0)
              error("ERROR on binding");

     listen(sockfd,5);
     clilen = sizeof(cli_addr);
     newsockfd = accept(sockfd,
                 (struct sockaddr *) &cli_addr,
                 &clilen);
     if (newsockfd < 0)
          error("ERROR on accept");
     bzero(buffer,256);
     n = read(newsockfd,buffer,255);
     if (n < 0) error("ERROR reading from socket");
     printf("%s\n",buffer);

}

int main(int argc, char** argv) {
    if ( argc==2 ) {
        if ( strcmp( argv[1], "speaker" )==0 ) {
            fprintf(stderr,"speaking...\n");
            beClient();
        } else if ( strcmp( argv[1], "listener" )==0 ) {
            fprintf(stderr,"listening...\n");
            beServer();
        } else {
            printUsage();
            exit(-1);
        }
    } else {
        printUsage();
        exit(-1);
    }

    return (EXIT_SUCCESS);
}

