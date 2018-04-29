Execution :
	$Makefile 			: produces the executable bidding_system, host, and player.
	$make bidding_system.c 		: produces the bidding_system.
	$make host.c 			: produces how the host will process.
	$make player.c 			: produces the players.
	$make clean			: removes the bidding_system, host, and player.

------------------------------------------------------------------------------------------

Description :
	Firstly, I wrote the bidding_system.c supported by reading the prof's ppt about how to fork() and about how to make pipe() between two programs.
	I forked the hosts in advance and connected them via pipes and listened to these pipes for communication. I also used I/O Multiplexing here as to know which fd is ready.

	Secondly, I wrote the player.c as I didn't really get what it meant for the host.c and it only needed to read and write from the FIFO between host and players. For the FIFO itself, the two parties needed to be connected, otherwise it would block the process.

	Finally, here comes to the hardest part that took me days to understand and debug. I wrote the host.c. It is like a mediator between bidding system and players so we have to connect it to bidding_system and players. In host.c , there is a code part when host sleep(1) after host forks all players. The reason is to ensure that all 4 players have been executed so the message from the host can be read by all 4 players.

------------------------------------------------------------------------------------------

Self-Examination :
	The program has already satisfied all 7 grading criteria.
	
	For the grading criteria 4, I have compiled my host with TA's bidding_system and 	player in workstation. It eventually produces the correct output.
	
	For the grading criteria 5, I have compiled my player with TA's bidding_system and 	host in workstation and it produces the correct output.

------------------------------------------------------------------------------------------
Additional Infomation on handling the error in host.c :

	If the player sends the wrong random_key or sends the money that is bigger than player's current money, then I handle it by directly assuming that the money the player will pay in this round is 0.