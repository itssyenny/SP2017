Execution :
	$Makefile 			: produces the executable server, file_reader.
	$make server.c 			: produces the web server.
	$make file_reader.c 		: produces a program to read the input.
	$make clean			: removes the server, file_reader.
—-----------------------------------------------------------------------————————————————————————————————————————————
Description :

Firstly, I wrote the server.c supported by reading the professor’s ppt about the signal handler.
I open the shared map which be used to get the latest CGI program data using mmap.Then, do multiplexing to handle many incoming connections by using select().The select() will block if there is no any ready fd. If ready fd is server.listen_fd, then I accept the incoming connection and change the requestP[I].status = READING. If ready fd is not server.listen_fd and the status is READING, then call read_header_and_file() function to get the CGI name (requestP[I].file) and filename (requestP[I].query).

If the returned value (ret = 0), then check whether the requestP[I].file = info or not.
If YES (INFO) , then server forks a child process, send SIGUSR1 signal to the server process(the parent), and exit(0), while the parent waits for the child process to die.

If NO (NOT INFO), get the file name without “filename=“ and then strcpy back again to requestP[I].query. After that, check the file validity and query validity. If the returned value from the validity() function is FALSE, then sends 400 Bad Request to browser, write, and clear requestP and close fd. If it returns TRUE, then fork and pipe for communication between parent and child. Do a redirection in both parent and child so child can read the input from stdin and send back responses to parent from stdout. Parent sends filename to child using pipe.

Otherwise, if requestP[I].status = WRITING, then check the child termination status If CGI name is not info, do writing if the status >= 0 and ready to send the responses back to web browser.

If status = 0 , then send the successful status 200 OK to web browser. Otherwise, send the 404 Not Found. After that, I record the exit time and file name to the shared map and finally clear the requestP and close fd.
If status >= 0 (CGI is info) , then get how many processes already die and get all pids of running processes and get the latest exited CGI file data from shared map. Finally, start constructing the response message to web.

Eventually, I remove the shared Mao and free the requestP.
—————————————————————————————————————————————————------------------------------------------------------------------
Self-Examination :

The program has already satisfied all 7 grading criteria.
—————————————————————————————————————————————————------------------------------------------------------------------
Additional Information on handling the error in server.c :

If I use Google Chrome, I need to handle the favicon info request from Google Chrome.

