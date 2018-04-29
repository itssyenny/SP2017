//b05902102 黃麗璿 
// file_reader = CGI program
// read a filename from STDIN
// write the content of the file to STDOUT 
// 	if the file is accessible
// 	if not, program should output error message to STDOUT + exit a nonzero return code
// ----------------------------------------------------------------------------------------
// execute CGI by fork() and communicate via pipe()
// CGI writes output to STDOUT,then return the content back to PARENT using only pipes.
// PARENT adds HTTP header to it + Outputs the result to the client.
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <netinet/in.h>

#define ERR_EXIT(a) { perror(a); exit(1); }
// return 0: success, file is buffered in retP->buf with retP->buf_len bytes
// return -1: error, check error code (*errP)
// return 1: read more, continue until return -1 or 0
// error code: 
// 1: client connection error 
// 2: bad request, cannot parse request
// 3: method not implemented 
// 4: illegal filename
// 5: illegal query
// 6: file not found
// 7: file is protected
//cara kirim ke servernya , cara read nya 
int main() {
	char message[1024];	//this is the filename
	char h1_NotFound[] = "Requested File is Protected\n";
	char h2_NotFound[] = "Requested File is Not Found\n";
 	struct stat sb;
	char *ptr;
	int n, r, fd;
	
	if((n = read(0, message, sizeof(message))) < 0) 
		fprintf(stderr, "read error in file reader\n");
	fprintf(stderr, "In file reader , n = %d\n", n );
	fprintf(stderr, "Message in file reader %s\n", message );
	message[n] = '\0';
	//retrieve information about the file pointed by the message.
	fprintf(stderr,"cgi sleep 5 seconds\n");
	sleep(10);
	fprintf(stderr,"cgi sleep done\n");

	r = stat(message, &sb);
    if ( r < 0 ) {
    	if(write(1, h2_NotFound, strlen(h2_NotFound)) < 0) ERR_EXIT("header 2 error");
    	fprintf(stderr, "In file_reader, file not found\n");
  		exit(6);	//file NOT FOUND
  	}

    //open the file message
    fd = open(message, O_RDONLY);
    if ( fd < 0 ) {
    	if(write(1, h1_NotFound, strlen(h1_NotFound)) <  0) ERR_EXIT("header 1 error\n");
    	fprintf(stderr, "In file_reader, file is protected\n");
    	exit(7);	//file is protected
   	}
    
    //read the file message all in one
	ptr = mmap( 0, (size_t) sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
	
	//write to the STDOUT
    if(write(1, ptr, strlen(ptr)) < 0) ERR_EXIT(" write to stdout is error\n");
    //pointer not need to point again to the file
	(void) munmap( ptr, sb.st_size );
	close( fd );

	exit(0);

}
