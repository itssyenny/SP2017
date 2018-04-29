#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
    char* filename;  // filename set in header, end with '\0'.
    int header_done;  // used by handle_read to know if the header is read or not.
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_header = "ACCEPT\n";
const char* reject_header = "REJECT\n";

// Forwards

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

static int handle_read(request* reqP);
// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error

int lock_reg(int fd, int cmd, int type, off_t offset, int whence, off_t len);
pid_t lock_test(int fd, int type, off_t offset, int whence, off_t len);
void set_fl(int fd, int flags);

int main(int argc, char** argv) {
    int i, ret;

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));

    // Get file descripter table size and initize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }

    int writefd[1000];
    for (i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);


    fd_set rset,readset;
    struct timeval timeout;

    FD_ZERO(&rset);
    FD_SET(svr.listen_fd, &rset);

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    
    maxfd = svr.listen_fd;  //maxfd = 3
    int tempfd,tmp;
    //int store;
    while (1) {
        // TODO: Add IO multiplexing
        FD_ZERO(&readset);
	    memcpy(&readset,&rset,sizeof(rset));

        // Check new connection
        int ready = select(maxfd+1, &readset, NULL, NULL, &timeout);
   
        if(ready > 0){ //if there is a file ready
            tempfd = maxfd;
	    	for(int i = svr.listen_fd; i <= maxfd; i++) {
                //printf("maxfd %d\n", maxfd);
                if(FD_ISSET(i, &readset)) {

                    if(i == svr.listen_fd) { //new connection for the server itself as listen_fd
                        clilen = sizeof(cliaddr);
                        conn_fd = accept(i, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen); //accept new conn_fd
                        
						if (conn_fd < 0) {
                            if (errno == EINTR || errno == EAGAIN) continue;  // try again
                            if (errno == ENFILE) {
                                (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                                continue;
                            }
                            ERR_EXIT("accept")
                        } //end of error if

                        int flags = fcntl(conn_fd, F_GETFL, 0);
                        fcntl(conn_fd, F_SETFL, flags | O_NONBLOCK); //dia gak akan pernah nunggu kalau client gak ada data.
                        FD_SET(conn_fd, &rset); //set the new connection e.g conn_fd = 4
                       
						if(conn_fd > tempfd) tempfd = conn_fd;
                        //file_fd = -1; // this is the first time
						writefd[conn_fd] = -1; //this is the first time write
                        requestP[conn_fd].conn_fd = conn_fd;
                        strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
                        fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
                    } //end of if(i == server)
                    else { //if it is not a server , IS CLIENTS, then process the requests
                    	#ifdef READ_SERVER
                            ret = handle_read(&requestP[i]);
                            if (ret < 0) {
                                fprintf(stderr, "bad request from %s\n", requestP[i].host);
                                continue;
                            }
                            if(ret == 0) break;

                            // requestP[conn_fd]->filename is guaranteed to be successfully set.
                            if (ret == 1) {
                                // open the file here.
                                fprintf(stderr, "Opening file [%s]\n", requestP[i].filename);
                                // TODO: Add lock
                                struct flock lock, savelock;
                                // TODO: check if the request should be rejected.
                                file_fd = open(requestP[i].filename, O_RDONLY);

                                lock.l_type = F_RDLCK;
                                lock.l_start = 0;
                                lock.l_whence = SEEK_SET;
                                lock.l_len = 0;

                                savelock = lock;

							    fcntl(file_fd,F_GETLK,&lock);
							   
							    if(lock.l_type == F_WRLCK){
                                    //fprintf(stderr,"reading failed\n");
			 				    	write(requestP[i].conn_fd,reject_header,sizeof(reject_header));
							    }
							    else{
	                                write(requestP[i].conn_fd, accept_header, sizeof(accept_header)); //ACCEPT
	                                
	                               	 fcntl(file_fd, F_SETLK, &savelock); //dipastiin lagi

	                                 //accept the request
                                     //sleep(10);
                                     //fprintf(stderr,"continue\n");
	                               	 while (1) { //keep reading untiil ret <= 0
	                                    
	                                    	ret = read(file_fd, buf, sizeof(buf));
	                                    	if (ret < 0) {
	                                        		fprintf(stderr, "Error when reading file %s\n", requestP[i].filename);
	                                        		//errorr = 1;
	                                        		break;
	                                    	} else if (ret == 0) break;

	                                    	buf[ret] = '\0'; //jadi ret akhir harus diset ke '\0' karena buf bisa keluar aneh"-> belakangnya yang aneh" dihapus

	                                    	write(requestP[i].conn_fd, buf, ret);
	                                	} //END OF WHILE
	                            
	                            	//Release unlocking : 
	                            	lock.l_type = F_UNLCK;
	                            	fcntl(file_fd, F_SETLK, &lock);
							    }

                                close(file_fd);        
                                if(requestP[i].conn_fd == tempfd) {
                                    tmp = svr.listen_fd;
                                    for(int j = svr.listen_fd+1; j < tempfd; j++){
                                        if(requestP[j].conn_fd != -1 && tmp < j){
                                            tmp = j;
                                        }
                                    }
                                    tempfd = tmp;
                                }

                                FD_CLR(requestP[i].conn_fd, &rset);  
                                close(requestP[i].conn_fd);
                                fprintf(stderr, "Done reading file [%s]\n", requestP[i].filename);
                                free_request(&requestP[i]);
		       				}
                    	#endif

                   		#ifndef READ_SERVER //WRITE SERVER
                            //fprintf(stderr, "Successfully enter the WRITE_SERVER with fd %d, writefd = %d, maxfd = %d\n", i,writefd[i],maxfd);
                            struct flock lock, savelock;
                            do {
                                ret = handle_read(&requestP[i]);
                                //printf("ret %d\n", ret);
                                if (ret < 0) {
                                    fprintf(stderr, "bad request from %s\n", requestP[i].host);
                                    continue; //pengaruh di apakah dia bakal loop terus buat nulis.
                                }
                                if (ret == 0) break;
                                
                                for(int j = svr.listen_fd+1; j <= maxfd && writefd[i] == -1; j++){
                                    if(j != i && requestP[j].conn_fd != -1 && writefd[j] != -1 && strcmp(requestP[i].filename,requestP[j].filename) == 0){
                                        //fprintf(stderr,"found the same filename with file fd = %d\n",writefd[j]);
                                        writefd[i] = -2;
                                    }
                                }

								// requestP[conn_fd]->filename is guaranteed to be successfully set.
                                if (ret == 1 && writefd[i] == -1) {//file can be access
                                    // open the file here.
                                    fprintf(stderr, "Opening file [%s]\n", requestP[i].filename);
                                    // TODO: Add lock
                                    // // TODO: check if the request should be rejected.
                                    // write(requestP[i].conn_fd, accept_header, sizeof(accept_header));

			                         writefd[i] = open(requestP[i].filename, O_WRONLY | O_CREAT | O_TRUNC,
			                                     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
							    	//fprintf(stderr,"success, file fd for process with fd %d is %d\n",i,writefd[i]);
			                    	lock.l_type = F_WRLCK;
			                    	lock.l_start = 0;
			                    	lock.l_whence = SEEK_SET;
			                    	lock.l_len = 0;

			                    	savelock = lock;
			                    
			                    	fcntl(writefd[i], F_GETLK, &lock);

			                    	if(lock.l_type == F_RDLCK || lock.l_type == F_WRLCK) {
			                        	//fprintf(stderr,"can't access file\n");
										write(requestP[i].conn_fd, reject_header, sizeof(reject_header)); //REJECT 
										//fprintf(stderr, "Done writing file [%s]\n", requestP[i].filename);
										
                                        if(requestP[i].conn_fd == tempfd) {
                                            tmp = svr.listen_fd;
                                            for(int j = svr.listen_fd+1; j < tempfd; j++){
                                                if(requestP[j].conn_fd != -1 && tmp < j){
                                                    tmp = j;
                                                }
                                            }
                                            tempfd = tmp;
                                        }

                                        close(writefd[i]);
										FD_CLR(requestP[i].conn_fd, &rset);
										close(requestP[i].conn_fd);

                                        free_request(&requestP[i]);
                                        break;
										//free_request(&requestP[i]);
									}
			                    	else {
										fcntl(writefd[i],F_SETLK, &savelock);
				                        write(requestP[i].conn_fd, accept_header, sizeof(accept_header)); //ACCEPT  
				                        write(writefd[i],requestP[i].buf,requestP[i].buf_len);
			    					}
                				} //end if ret == 1
								else if(ret == 1 && writefd[i] > 0){
                                    //fprintf(stderr,"just write to the file %s\n",requestP[i].filename);
									write(writefd[i],requestP[i].buf,requestP[i].buf_len);
                                }
                                else if(writefd[i] == -2){
                                    //fprintf(stderr,"there is another client open the file, send reject header\n");
                                    write(requestP[i].conn_fd,reject_header,sizeof(reject_header));
                                    
                                    if(requestP[i].conn_fd == tempfd) {
                                        tmp = svr.listen_fd;
                                        for(int j = svr.listen_fd+1; j < tempfd; j++){
                                            if(requestP[j].conn_fd != -1 && tmp < j){
                                                tmp = j;
                                            }
                                        }
                                        tempfd = tmp;
                                    }
                                    
                                    //close(writefd[i]);
                                    FD_CLR(requestP[i].conn_fd, &rset);
                                    close(requestP[i].conn_fd);
                                    free_request(&requestP[i]);
                                    break;
                                }
                            } while (ret > 0);

                            if(ret == 0) {
                            	fprintf(stderr, "Done writing file [%s]\n", requestP[i].filename);

                                //fprintf(stderr, "writefd[%d] %d\n",i, writefd[i] );
                                if(writefd[i] > 0) {
                                    //Release unlocking :
                                    lock.l_type =  F_UNLCK;
								    if(fcntl(writefd[i], F_SETLK, &lock) < 0) 
                                        //fprintf(stderr,"release lock file error for fd %d filename = %s\n",i,requestP[i].filename);

                                    close(writefd[i]);
                                }

                                //clear request

                                if(requestP[i].conn_fd == tempfd) {
                                    tmp = svr.listen_fd;
                                    for(int j = svr.listen_fd+1; j < tempfd; j++){
                                        if(requestP[j].conn_fd != -1 && tmp < j){
                                            tmp = j;
                                        }
                                    }
                                    tempfd = tmp;
                                }
								FD_CLR(requestP[i].conn_fd, &rset);  
                                close(requestP[i].conn_fd);
                                free_request(&requestP[i]);
                            }  
                   		#endif
                    } // end of else if(i == server)
                } //if fd_isset
            } // end for ( i = 0 to maxfd) 
		} //end of else  of select return < 0 
		maxfd = tempfd;
    } //end of while
    free(requestP);
    return 0;
}


int lock_reg(int fd, int cmd, int type, off_t offset, int whence, off_t len) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = type;
    lock.l_whence = whence;
    lock.l_start = offset;
    lock.l_len = len; //exception

    int x = fcntl(fd, cmd, &lock);
    return x;
}

pid_t lock_test(int fd, int type, off_t offset, int whence, off_t len) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0; //exception

    if(fcntl(fd, F_GETLK, &lock) < 0) ERR_EXIT("fcntl error");

    if(lock.l_type == F_UNLCK) return 0;

    return(lock.l_pid);
}
void set_fl(int fd, int flags) {
    int val;
    val = fcntl(fd, F_GETFL, 0);
    val |= flags;
    fcntl(fd, F_SETFL, val);
}


// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void* e_malloc(size_t size);


static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->filename = NULL;
    reqP->header_done = 0;
}

static void free_request(request* reqP) {
    if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }
    init_request(reqP);
}

// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error
static int handle_read(request* reqP) {
    int r;
    char buf[512];

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    if (reqP->header_done == 0) {
        char* p1 = strstr(buf, "\015\012");
        int newline_len = 2;
        // be careful that in Windows, line ends with \015\012
        if (p1 == NULL) {
            p1 = strstr(buf, "\012");
            newline_len = 1;
            if (p1 == NULL) {
                // This would not happen in testing, but you can fix this if you want.
                ERR_EXIT("header not complete in first read...");
            }
        }
        size_t len = p1 - buf + 1;
        reqP->filename = (char*)e_malloc(len);
        memmove(reqP->filename, buf, len);
        reqP->filename[len - 1] = '\0';
        p1 += newline_len;
        reqP->buf_len = r - (p1 - buf);
        memmove(reqP->buf, p1, reqP->buf_len);
        reqP->header_done = 1;
    } else {
        reqP->buf_len = r;
        memmove(reqP->buf, buf, r);
    }
    return 1;
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }
}

static void* e_malloc(size_t size) {
    void* ptr;

    ptr = malloc(size);
    if (ptr == NULL) ERR_EXIT("out of memory");
    return ptr;
}

