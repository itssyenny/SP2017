//b05902102 黃麗璿 
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define TIMEOUT_SEC 5       // timeout in seconds for wait for a connection 
#define MAXBUFSIZE  1024    // timeout in seconds for wait for a connection 
#define NO_USE      0       // status of a http request
#define ERROR       -1  
#define READING     1       
#define WRITING     2       
#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct {
    char hostname[512];     // hostname
    unsigned short port;    // port to listen
    int listen_fd;      // fd to wait for a new connection
} http_server;

typedef struct {
    int conn_fd;        // fd to talk with client
    int status;         // not used, error, reading (from client)
    int child_terminated_status;
                                // writing (to client)
    char file[MAXBUFSIZE];  // requested file
    char query[MAXBUFSIZE]; // requested query
    char host[MAXBUFSIZE];  // client host
    char* buf;          // data sent by/to client
    size_t buf_len;     // bytes used by buf
    size_t buf_size;        // bytes allocated for buf
    size_t buf_idx;         // offset for reading and writing
} http_request;

typedef struct {
    char c_time_string[1240];
    char filename[1240];
} TimeInfo;

static char* logfilenameP;  // log file name
http_server server;     // http server
http_request* requestP = NULL;// pointer to http requests from client

int received = 0;
int status, final_status[1024];
pid_t pid[120];
int maxfd = 0;             // size of open file descriptor table
pid_t pid1;
static void sig_usr(int);
// Forwards
//
static void init_http_server( http_server *svrP,  unsigned short port );
// initailize a http_request instance, exit for error

static void init_request( http_request* reqP );
// initailize a http_request instance

static void free_request( http_request* reqP );
// free resources used by a http_request instance

static int read_header_and_file( http_request* reqP, int *errP );
// return 0: success, file is buffered in retP->buf with retP->buf_len bytes
// return -1: error, check error code (*errP)
// return 1: continue to it until return -1 or 0
// error code: 
// 1: client connection error 
// 2: bad request, cannot parse request
// 3: method not implemented 
// 4: illegal filename
// 5: illegal query
// 6: file not found
// 7: file is protected

static void add_to_buf( http_request *reqP, char* str, size_t len );
static void set_ndelay( int fd );
// Set NDELAY mode on a socket.
static void err_doit(int errnoflag, int error, const char *fmt, va_list ap) {
    char buf[1024];
    vsnprintf(buf, 1024, fmt, ap);
    if (errnoflag)
       snprintf(buf+strlen(buf), 1024-strlen(buf), ": %s",
         strerror(error));
    strcat(buf, "\n");
    fflush(stdout);     /* in case stdout and stderr are the same */
    fputs(buf, stderr);
    fflush(NULL);       /* flushes all stdio output streams */
}
void err_dump(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    err_doit(1, errno, fmt, ap);
    va_end(ap);
    abort();        /* dump core and terminate */
    exit(1);        /* shouldn't get here */
}
int validity(char string[], int index) {//index = 0, check cgi name, index = 1, check filename
    int size = strlen(string);
    // fprintf(stderr, "Size of the string %d\n", size );
    //int flag = 1;
 
    if(index == 0){//check validity cgi name
//         if(strcmp(string,"file_reader") != 0 && strcmp(string,"info") != 0){
//             return 0;
        }
        else{
            for(int i  = 0; i < size; i++) {
                if(isalpha(string[i]) || isupper(string[i]) || isdigit(string[i]) || string[i] == '_'){}
                else {
                    return 0;
                }
            }
        }
    }
    else{//check validity of file name
        for(int i  = 0; i < size; i++) {
            if(isalpha(string[i]) || isupper(string[i]) || isdigit(string[i]) || string[i] == '_') {
                // fprintf(stderr,"char = %c\n",string[i]);
            }
            else {
                return 0;
            }
        }
    }
    return 1;
}

int main( int argc, char** argv ) {


    struct sockaddr_in cliaddr; // used by accept()
    int clilen;

    int conn_fd;        // fd for a new connection with client
    int err;            // used by read_header_and_file()
    int i, ret, nwritten, logfd;

    int buflen, status = -4, left_The_Program = 0;
    char buffer[1024], buf[1024];
    char timebuf[1024];
    struct stat sb;

    TimeInfo *p_map;
    time_t current_time;
    int pfd1[120][2], pfd2[120][2];

    //signal handler
    if(signal(SIGUSR1, sig_usr) == SIG_ERR) ERR_EXIT("can't catch SIGUSR1\n");
    if(signal(SIGCHLD, sig_usr) == SIG_ERR) ERR_EXIT("can't catch SIGUSR1\n");

    // Parse args. 
    if ( argc != 3 ) {
        (void) fprintf( stderr, "usage:  %s port# logfile\n", argv[0] );
        exit( 1 );
    }

    logfilenameP = argv[2];

    //Open share map to get the lastest cgi program data
    logfd = open(logfilenameP, O_RDWR | O_TRUNC | O_CREAT, 0777);
    if(logfd < 0) ERR_EXIT("open file error\n");
    lseek(logfd, sizeof(TimeInfo), SEEK_SET);
    write(logfd, "", 1);

    p_map = (TimeInfo*) mmap(0, sizeof(TimeInfo), PROT_READ|PROT_WRITE, MAP_SHARED, logfd, 0);
    // printf("mmap address:%#x\n",(unsigned int)&p_map); // 0x00000
    close(logfd);

    // Initialize http server
    init_http_server( &server, (unsigned short) atoi( argv[1] ) );

    maxfd = getdtablesize();
    requestP = ( http_request* ) malloc( sizeof( http_request ) * maxfd );
    if ( requestP == (http_request*) 0 ) {
        fprintf( stderr, "out of memory allocating all http requests\n" );
        exit( 1 );
    }

    for ( i = 0; i < maxfd; i ++ )
        init_request( &requestP[i] );
    set_ndelay(server.listen_fd);   //addition
    requestP[ server.listen_fd ].conn_fd = server.listen_fd;
    requestP[ server.listen_fd ].status = READING;

    fprintf( stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d, logfile %s...\n", server.hostname, server.port, server.listen_fd, maxfd, logfilenameP );

    // Main loop. 
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    fd_set reset, used;
    FD_ZERO(&reset);
    FD_ZERO(&used);
    FD_SET(server.listen_fd, &reset);
    maxfd = server.listen_fd;
    int tempfd = maxfd; //no need

    //master set = reset
    while(1) {
        FD_ZERO(&used);
        memcpy(&used, &reset, sizeof(reset));

        tempfd = maxfd;
        fprintf(stderr, "\nmaxfd %d\n", maxfd);
        int ready = select(maxfd+1, &used, NULL, NULL, &timeout);

        // fprintf(stderr,"after select\n");
        // if(ready > 0) {
        for(int i = server.listen_fd; i <= maxfd; i++) {
        //If there is a data ready
            //fprintf(stderr, "i %d, status = %d\n",i,requestP[i].status);
            if(FD_ISSET(i, &used) != 0) {
                fprintf(stderr,"ENTER FD_ISSET, for i = %d, status = %d, file = %s, query = %s\n",i,requestP[i].status,requestP[i].file,requestP[i].query);
                // Wait for a connection.
                if(i == server.listen_fd) { 
                    clilen = sizeof(cliaddr);
                    conn_fd = accept( server.listen_fd, (struct sockaddr *) &cliaddr, (socklen_t *) &clilen );

                    if ( conn_fd < 0 ) {
                        if ( errno == EINTR || errno == EAGAIN ) continue; // try again 
                        if ( errno == ENFILE ) {
                            (void) fprintf( stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd );
                            continue;
                        }   
                        ERR_EXIT( "accept" )
                    } //end of error if
                    FD_SET(conn_fd, &reset);

                    if(conn_fd > tempfd) tempfd = conn_fd;

                    requestP[conn_fd].conn_fd = conn_fd;
                    //change the status to READING  -> THE DATA IS READY TO PROCESS NOW
                    requestP[conn_fd].status = READING;     
                    strcpy( requestP[conn_fd].host, inet_ntoa( cliaddr.sin_addr ) );
                    set_ndelay( conn_fd );
                    
                    fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host );
                }   //end of server's part
                else if(requestP[i].status == READING){//do reading if only request status = reading
                    // fprintf(stderr, "Go to Client\n");
                    // fprintf(stderr, "i :%d\n", i);
                    
                    //to get cgi name and filename
                    ret = read_header_and_file( &requestP[i], &err );

                    if ( ret < 0 ) {
                    // error for reading http header or requested file
                        fprintf( stderr, "error on fd %d, code %d\n", requestP[i].conn_fd, err );
                        requestP[i].status = ERROR;
                        FD_CLR(requestP[i].conn_fd, &reset);
                        close( requestP[i].conn_fd);
                        free_request( &requestP[i] );
                        break;
                    }
                    else if ( ret > 0 ) continue;
                    else if ( ret == 0 ) {
                        //Time to fork :
                        // fprintf(stderr, "In RET = 0, cgi %s\n", requestP[i].file );
                        // fprintf(stderr, "In RET = 0, filename %s\n", requestP[i].query );

                        if(strcmp(requestP[i].file, "info") == 0) { 
                            //change the status to be WRITING
                            requestP[i].status = WRITING;

                            //Server forks a child process
                            if((pid[i] = fork()) == 0) {
                                //and the child process sends SIGUSR1 signal to the server process(the parent)
                                kill(getppid(), SIGUSR1);
                                exit(0);
                            } else {
                                waitpid(pid[i], 0, WNOHANG);
                            }
                        }
                        else {    //CGI IS NOT INFO
                            //get filename
                            // fprintf(stderr, "ENTER THE strtok\n");
                            char *token;
                            char str[1024];
                            strcpy(str,requestP[i].query);
                            // fprintf(stderr, "1\n");
                            token = strtok(str, "=");
                            token = strtok(NULL, "=");

                            if(token != NULL) {
                                strcpy(requestP[i].query, token);
                                // printf("rquets %c\n", requestP[i].query[sizeof(requestP[i].query)-1] );
                            }
 
                            //fprintf(stderr, "After strtok %c\n", requestP[i].query);
                            int file_validity = validity(requestP[i].file,0);
                            int query_validity = validity(requestP[i].query,1);
                            if(file_validity == 0 || query_validity == 0){
                                // fprintf(stderr,"not valid\n");

                                if(file_validity == 0) sprintf(buffer,"INVALID CGI NAME\n");
                                else if(query_validity == 0) sprintf(buffer,"INVALID FILE NAME\n");

                                requestP[i].buf_len = 0;
                                buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 400 Bad Request\015\012Server: SP TOY\015\012" );
                                add_to_buf( &requestP[i], buf, buflen );

                                current_time = time( (time_t*) 0 );
                                (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &current_time ) );
                                buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
                                add_to_buf( &requestP[i], buf, buflen );

                                buflen = snprintf( buf, sizeof(buf), "Content-Length: %lld\015\012", (int64_t) strlen(buffer));
                                add_to_buf( &requestP[i], buf, buflen );
                             
                                buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
                                add_to_buf( &requestP[i], buf, buflen );

                                add_to_buf( &requestP[i], buffer, strlen(buffer) );

                                // write once only and ignore error
                                nwritten = write( requestP[i].conn_fd, requestP[i].buf, requestP[i].buf_len );
                                fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[i].conn_fd );

                                //Clear requestP and close fd 
                                FD_CLR(requestP[i].conn_fd, &reset);
                                close( requestP[i].conn_fd );
                                free_request( &requestP[i] );
                            } else {  //VALID
                                //STDIN : 0
                                //STDOUT : 1

                                if(pipe(pfd1[i]) < 0) ERR_EXIT("pipe pfd1 error");
                                if(pipe(pfd2[i]) < 0) ERR_EXIT("pipe pfd2 error");

                                if((pid[i] = fork()) == 0) {
                                    printf("CHILD\n");
                                    dup2(pfd2[i][1],1); //tell back the server the results (write)
                                    dup2(pfd1[i][0],0); //receive the commands from server (read)
                                    close(pfd1[i][1]);
                                    close(pfd2[i][0]);

                                    if(execl("./file_reader", "file_reader", NULL) < 0){ 
                                        ERR_EXIT("IN CHILD, execl error");
                                    }

                                } else {
                                    //parent
                                    printf("PARENT\n");
                                    close(pfd1[i][0]);
                                    close(pfd2[i][1]);
                                    //Parent sends filename to child using pipe
                                    fprintf(stderr, "Filename in parent : %s\n",requestP[i].query);
                                    if(write(pfd1[i][1], requestP[i].query, strlen(requestP[i].query)) < 0) 
                                        ERR_EXIT("Error writing to child\n");
                                }   //end parent
                            } //end of VALID
                        } //end CGI IS NOT INFO
                    } //end ret = 0
                } //end if requestP[i].status == reading
            } //end FD_ISSET
            else if(requestP[i].status == WRITING) {
                fprintf(stderr,"enter if status i writing for i = %d\n",i);
                //check child termination status
                // status = -1;
                // sig_usr(SIGCHLD);

                // waitpid(pid[i], &status, WNOHANG);
                //status > 0 , error (gak ada child yang dead).
                fprintf(stderr,"status = %d\n",requestP[i].child_terminated_status);

                //do writing only if status >= 0 (child already die)
                if(requestP[i].child_terminated_status >= 0 && strcmp(requestP[i].file, "info") != 0) {
                    //Ready to send the responses back to the web browser
                    memset(buffer, 0, sizeof(buffer));

                    if(read(pfd2[i][0], buffer, sizeof(buffer)) < 0) 
                        ERR_EXIT("error reading in the writing\n");

                    if(requestP[i].child_terminated_status == 0) {
                        requestP[i].buf_len = 0;

                        buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 200 OK\015\012Server: SP TOY\015\012" );
                        add_to_buf( &requestP[i], buf, buflen );
                        

                        current_time = time( (time_t*) 0 );
                        (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &current_time ) );
                        buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
                        add_to_buf( &requestP[i], buf, buflen );
                       

                        buflen = snprintf( buf, sizeof(buf), "Content-Length: %lld\015\012", (int64_t) strlen(buffer));
                        add_to_buf( &requestP[i], buf, buflen );
                     

                        buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
                        add_to_buf( &requestP[i], buf, buflen );
                    

                        add_to_buf( &requestP[i], buffer, strlen(buffer) );
                        // fprintf(stderr, "*** Buffer[%d] : %s\n", i, buffer );
                     
                    }
                    else {  //if the status is not zero
                        requestP[i].buf_len = 0;
                        buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 404 Not Found\015\012Server: SP TOY\015\012" );
                        add_to_buf( &requestP[i], buf, buflen );
                       

                        current_time = time( (time_t*) 0 );
                        (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &current_time ) );
                        buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
                        add_to_buf( &requestP[i], buf, buflen );
                       

                        buflen = snprintf( buf, sizeof(buf), "Content-Length: %lld\015\012", (int64_t) strlen(buffer));
                        add_to_buf( &requestP[i], buf, buflen );
                     

                        buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
                        add_to_buf( &requestP[i], buf, buflen );
                   

                        add_to_buf( &requestP[i], buffer, strlen(buffer));
                       
                    }

                    // write once only and ignore error
                    nwritten = write( requestP[i].conn_fd, requestP[i].buf, requestP[i].buf_len );
                    fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[i].conn_fd );

                    //Record the exit time and filename to the share map
                    memset(buffer,0,sizeof(buffer));

                    current_time = time(NULL);
                    strcpy(buffer, ctime(&current_time));
                    buffer[strlen(buffer)-1] = '\0';

                    // fprintf(stderr,"before: %s,%s want change to: %s,%s\n",p_map->c_time_string,p_map->filename,buffer,requestP[i].query);
                    memset(p_map,0,sizeof(TimeInfo));

                    memcpy(p_map->c_time_string, buffer, strlen(buffer));
                    memcpy(p_map->filename, requestP[i].query, strlen(requestP[i].query));
                   
                    left_The_Program++;

                    //Clear requestP and close fd 
                    FD_CLR(requestP[i].conn_fd, &reset);
                    close( requestP[i].conn_fd );
                    free_request( &requestP[i] );
                    close(pfd2[i][0]);
                    close(pfd1[i][1]);
                } //end STRCMP != INFO
                else if(received > 0){  //CGI IS INFO, status always >= 0
                    fprintf(stderr, "Enter the INFO\n");
                    //Parent has received signal from child
                        
                        //Ready to send responses to web browser
                        memset(buffer,0,sizeof(buffer));
                        
                        //get how many processed already die
                        sprintf(buf, "%d processes die previously\n", left_The_Program);
                        strcat(buffer,buf);
                        buffer[strlen(buffer)] = '\0';

                        //get all pids of running processes
                        sprintf( buf, "PIDs of Running Processes: ");
                        strcat(buffer,buf);
                        buffer[strlen(buffer)] = '\0';

                        //PID of current running process by requestP[i].status > 0
                        int start = 0;
                        for(int j = server.listen_fd+1; j <= maxfd; j++){
                            if(j != i && requestP[j].status > 0 && strcmp(requestP[j].file,"file_reader") == 0){
                                fprintf(stderr, "CHECKING THE HECK i = %d, j : %d status : %d, query = %s, file = %s\n", i, j, requestP[j].status,requestP[j].query,requestP[j].file);
                                if(start != 0){
                                    sprintf(buf,", ");
                                    strcat(buffer,buf);
                                    buffer[strlen(buffer)] = '\0';
                                }
                                fprintf(stderr, "PID[%d] %d\n", j, pid[j] );
                                // fprintf(stderr, "%s\n", requestP[i].buf );
                                sprintf(buf,"%d",pid[j]);
                                strcat(buffer,buf);
                                buffer[strlen(buffer)] = '\0';

                                start++;
                            }
                        }

                        sprintf(buf,"\n");
                        strcat(buffer,buf);
                        buffer[strlen(buffer)] = '\0';

                        //get latest exited CGI file data from shared map
                        sprintf( buf, "Last Exit CGI: %s, Filename: %s\n", p_map->c_time_string, p_map->filename);
                        strcat(buffer,buf);
                        buffer[strlen(buffer)] = '\0';

                        //start construct respond message to web
                        requestP[i].buf_len = 0;
                        buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 200 OK\015\012Server: SP TOY\015\012" );
                        add_to_buf( &requestP[i], buf, buflen );
                        

                        current_time = time( (time_t*) 0 );
                        (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &current_time ) );
                        buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
                        add_to_buf( &requestP[i], buf, buflen );
                       

                        buflen = snprintf( buf, sizeof(buf), "Content-Length: %lld\015\012", (int64_t) strlen(buffer));
                        add_to_buf( &requestP[i], buf, buflen );
                     

                        buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
                        add_to_buf( &requestP[i], buf, buflen );
                    

                        add_to_buf( &requestP[i], buffer, strlen(buffer) );

                        // write once only and ignore error
                        nwritten = write( requestP[i].conn_fd, requestP[i].buf, requestP[i].buf_len );
                        fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[i].conn_fd );

                        //Clear requestP and close fd 
                        FD_CLR(requestP[i].conn_fd, &reset);
                        close( requestP[i].conn_fd );
                        free_request( &requestP[i] );

                        //After one process has been processed, subtract the received by 1 :
                        received--;
                    // }
                }
            } //end IF status = WRITING
        } //end FOR

        //check current maxfd
        maxfd = -1;
        for(int j = server.listen_fd; j <= tempfd; j++){ 
            // fprintf(stderr,"j = %d, status = %d\n",j,requestP[j].status);
            if(j > maxfd && requestP[j].status > 0) maxfd = j;
        }
    } //end WHILE(1)
    //remove shared map
    munmap(p_map, sizeof(TimeInfo));
    fprintf(stderr, "umap OK\n");
    // printf("umap ok \n");
    free(requestP);

    return 0;
}

static void sig_usr(int signo) {
    if(signo == SIGUSR1) {
        received++;
        printf("received SIGUSR1\n");
    } else if(signo == SIGCHLD) {// if there is CGI program DIE
        //to get pid of died child and it's termination status
        pid1 = wait(&status);

        fprintf(stderr,"received SIGCHILD, pid1 = %d, status = %d\n",pid1,status);

        //search through data pid and compare which child's pid is match with pid1
        for(int i = server.listen_fd+1; i <= maxfd; i++) {
            if(pid[i] == pid1 && requestP[i].status > 0) {
                // fprintf(stderr,"found at i = %d\n",i);
                requestP[i].status = WRITING;
                requestP[i].child_terminated_status = status;
                break;
            }
        }
    }else err_dump("received signal %d\n",signo);
}


// ======================================================================================================
// You don't need to know current_time how the following codes are working

#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>

//static void add_to_buf( http_request *reqP, char* str, size_t len );
static void strdecode( char* to, char* from );
static int hexit( char c );
static char* get_request_line( http_request *reqP );
static void* e_malloc( size_t size );
static void* e_realloc( void* optr, size_t size );

static void init_request( http_request* reqP ) {
    reqP->conn_fd = -1;
    reqP->status = 0;		// not used
    reqP->file[0] = (char) 0;
    reqP->query[0] = (char) 0;
    reqP->host[0] = (char) 0;
    reqP->buf = NULL;
    reqP->buf_size = 0;
    reqP->buf_len = 0;
    reqP->buf_idx = 0;
}

static void free_request( http_request* reqP ) {
    if ( reqP->buf != NULL ) {
	free( reqP->buf );
	reqP->buf = NULL;
    }
    init_request( reqP );
}


#define ERR_RET( error ) { *errP = error; return -1; }
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
//
static int read_header_and_file( http_request* reqP, int *errP ) {
    // Request variables
    char* file = (char *) 0;
    char* path = (char *) 0;
    char* query = (char *) 0;
    char* protocol = (char *) 0;
    char* method_str = (char *) 0;
    int r, fd;
    struct stat sb;
    char timebuf[100];
    int buflen;
    char buf[10000];
    time_t current_time;
    void *ptr;

    // Read in request from client
    while (1) {
    	r = read( reqP->conn_fd, buf, sizeof(buf) );

    	if ( r < 0 && ( errno == EINTR || errno == EAGAIN ) ) return 1;
    	if ( r <= 0 ) ERR_RET( 1 )
    	add_to_buf( reqP, buf, r );
    	if (strstr( reqP->buf, "\015\012\015\012" ) != (char*) 0 || strstr( reqP->buf, "\012\012" ) != (char*) 0 ) break;
    }
    
    //fprintf( stderr, "header: %s\n", reqP->buf );
    
    if(strstr(reqP->buf,"favicon.ico") != NULL){
        fprintf(stderr,"it's get favicon request, ignore it\n");
        ERR_RET(2);
    }

    // Parse the first line of the request.
    method_str = get_request_line( reqP );
    if ( method_str == (char*) 0 ) ERR_RET( 2 )
    path = strpbrk( method_str, " \t\012\015" );
    if ( path == (char*) 0 ) ERR_RET( 2 )
    *path++ = '\0';
    path += strspn( path, " \t\012\015" );
    protocol = strpbrk( path, " \t\012\015" );
    if ( protocol == (char*) 0 ) ERR_RET( 2 )
    *protocol++ = '\0';
    protocol += strspn( protocol, " \t\012\015" );
    query = strchr( path, '?' );
    if ( query == (char*) 0 )
	query = "";
    else
	*query++ = '\0';

    if ( strcasecmp( method_str, "GET" ) != 0 ) ERR_RET( 3 )
    else {
        strdecode( path, path );
        if ( path[0] != '/' ) ERR_RET( 4 )
	else file = &(path[1]);
    }

    if ( strlen( file ) >= MAXBUFSIZE-1 ) ERR_RET( 4 )
    if ( strlen( query ) >= MAXBUFSIZE-1 ) ERR_RET( 5 )
	  
    strcpy( reqP->file, file );
    strcpy( reqP->query, query );
    // if(ptrtok != NULL) strcpy(reqP->query, ptrtok);

    /*
    if ( query[0] == (char) 0 ) {
        // for file request, read it in buf
        r = stat( reqP->file, &sb );
        if ( r < 0 ) ERR_RET( 6 )

        fd = open( reqP->file, O_RDONLY );
        if ( fd < 0 ) ERR_RET( 7 )

	reqP->buf_len = 0;

        buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 200 OK\015\012Server: SP TOY\015\012" );
        add_to_buf( reqP, buf, buflen );
     	current_time = time( (time_t*) 0 );
        (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &current_time ) );
        buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
        add_to_buf( reqP, buf, buflen );
	buflen = snprintf(
	    buf, sizeof(buf), "Content-Length: %ld\015\012", (int64_t) sb.st_size );
        add_to_buf( reqP, buf, buflen );
        buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
        add_to_buf( reqP, buf, buflen );

	ptr = mmap( 0, (size_t) sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
	if ( ptr == (void*) -1 ) ERR_RET( 8 )
        add_to_buf( reqP, ptr, sb.st_size );
	(void) munmap( ptr, sb.st_size );
	close( fd );
	// printf( "%s\n", reqP->buf );
	// fflush( stdout );
	reqP->buf_idx = 0; // writing from offset 0
	return 0;
    }
    */

    return 0;
}


static void add_to_buf( http_request *reqP, char* str, size_t len ) { 
    char** bufP = &(reqP->buf);
    size_t* bufsizeP = &(reqP->buf_size);
    size_t* buflenP = &(reqP->buf_len);

    if ( *bufsizeP == 0 ) {
	*bufsizeP = len + 500;
	*buflenP = 0;
	*bufP = (char*) e_malloc( *bufsizeP );
    } else if ( *buflenP + len >= *bufsizeP ) {
	*bufsizeP = *buflenP + len + 500;
	*bufP = (char*) e_realloc( (void*) *bufP, *bufsizeP );
    }
    (void) memmove( &((*bufP)[*buflenP]), str, len );
    *buflenP += len;
    (*bufP)[*buflenP] = '\0';
}

static char* get_request_line( http_request *reqP ) { 
    int begin;
    char c;

    char *bufP = reqP->buf;
    int buf_len = reqP->buf_len;

    for ( begin = reqP->buf_idx ; reqP->buf_idx < buf_len; ++reqP->buf_idx ) {
	c = bufP[ reqP->buf_idx ];
	if ( c == '\012' || c == '\015' ) {
	    bufP[reqP->buf_idx] = '\0';
	    ++reqP->buf_idx;
	    if ( c == '\015' && reqP->buf_idx < buf_len && 
	        bufP[reqP->buf_idx] == '\012' ) {
		bufP[reqP->buf_idx] = '\0';
		++reqP->buf_idx;
	    }
	    return &(bufP[begin]);
	}
    }
    fprintf( stderr, "http request format error\n" );
    exit( 1 );
}



static void init_http_server( http_server *svrP, unsigned short port ) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname( svrP->hostname, sizeof( svrP->hostname) );
    svrP->port = port;
   
    svrP->listen_fd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( svrP->listen_fd < 0 ) ERR_EXIT( "socket" )

    bzero( &servaddr, sizeof(servaddr) );
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl( INADDR_ANY );
    servaddr.sin_port = htons( port );
    tmp = 1;
    if ( setsockopt( svrP->listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*) &tmp, sizeof(tmp) ) < 0 ) 
	ERR_EXIT ( "setsockopt " )
    if ( bind( svrP->listen_fd, (struct sockaddr *) &servaddr, sizeof(servaddr) ) < 0 ) ERR_EXIT( "bind" )

    if ( listen( svrP->listen_fd, 1024 ) < 0 ) ERR_EXIT( "listen" )
}

// Set NDELAY mode on a socket.
static void set_ndelay( int fd ) {
    int flags, newflags;

    flags = fcntl( fd, F_GETFL, 0 );
    if ( flags != -1 ) {
	newflags = flags | (int) O_NDELAY; // nonblocking mode
	if ( newflags != flags )
	    (void) fcntl( fd, F_SETFL, newflags );
    }
}   

static void strdecode( char* to, char* from ) {
    for ( ; *from != '\0'; ++to, ++from ) {
	if ( from[0] == '%' && isxdigit( from[1] ) && isxdigit( from[2] ) ) {
	    *to = hexit( from[1] ) * 16 + hexit( from[2] );
	    from += 2;
	} else {
	    *to = *from;
        }
    }
    *to = '\0';
}


static int hexit( char c ) {
    if ( c >= '0' && c <= '9' )
	return c - '0';
    if ( c >= 'a' && c <= 'f' )
	return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' )
	return c - 'A' + 10;
    return 0;           // shouldn't happen
}


static void* e_malloc( size_t size ) {
    void* ptr;

    ptr = malloc( size );
    if ( ptr == (void*) 0 ) {
	(void) fprintf( stderr, "out of memory\n" );
	exit( 1 );
    }
    return ptr;
}


static void* e_realloc( void* optr, size_t size ) {
    void* ptr;

    ptr = realloc( optr, size );
    if ( ptr == (void*) 0 ) {
	(void) fprintf( stderr, "out of memory\n" );
	exit( 1 );
    }
    return ptr;
}
