/*B05902102 黃麗璿*/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include<sys/types.h>
#include<sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include<fcntl.h>

#define ERR_EXIT(a) { perror(a); exit(1); }
/*./player [host_id] [player_index] [random_key] */
int main(int argc, char *argv[]) {
    int host_id = atoi(argv[1]);
    char player_index[2];
    strcpy(player_index,argv[2]);
    player_index[strlen(player_index)] = '\0';
    int random_key = atoi(argv[3]);
    
    //Open all FIFO to communicate with the hosts :
    //Open FIFO :
    char readFIFO[1024], writeFIFO[1024];
    
    //the player reads the message from host[host_id]_[player_index].FIFO
    sprintf(readFIFO, "host%d_%s.FIFO", host_id, player_index); //host1_A.FIFO
    int FIFO_R = open(readFIFO, O_RDWR);
    
    //and then writes the message received into the host[host_id].FIFO
    sprintf(writeFIFO, "host%d.FIFO", host_id); //host1.FIFO
    int FIFO_W = open(writeFIFO, O_RDWR);
    
    //fprintf(stderr,"fifo host = %s, fifo player = %s\n",readFIFO,writeFIFO);
    //Each time a player receives a message from host, each player has to send RESPONSES
    
    //fprintf(stderr,"IN PLAYER %s host %d, welcome to player : %d\n",player_index,host_id,random_key);
    char buf[1024];
    char output[1024];
    int money[4], money_to_pay = 0;
    int The_Money = 0, The_Turn = -1;
    
    int idx = player_index[0] - 'A';
    int n;
    
    for(int i = 0; i < 10; i++) {
        memset(buf,0,sizeof(buf));
        memset(output,0,sizeof(output));
        
        n = read(FIFO_R, buf, sizeof(buf));
        //fprintf(stderr,"IN PLAYER %s host %d,n = %d\n",player_index,host_id,n);
        buf[n-1] = '\0';
        
        //fprintf(stderr,"IN PLAYER %s host %d, message from host = %s\n",player_index,host_id,buf);
        sscanf(buf, "%d %d %d %d\n", &money[0], &money[1], &money[2], &money[3]);
        
        The_Money = money[idx];
        
        The_Turn = i % 4;
        if(The_Turn == idx) {
            //here comes the turn of player[player_index] :
            money_to_pay = The_Money;
        }
        else{
            money_to_pay = 0;
        }
        
        //After receiving the message from host, 就馬上 sends a response to the host
        //[player_index] [random_key] [money] :
        
        sprintf(output, "%s %d %d\n", player_index, random_key, money_to_pay);
        //fprintf(stderr,"IN PLAYER %s host %d, message to host = %s",player_index,host_id,output);
        write(FIFO_W, output, strlen(output));
        fsync(FIFO_W);
    }
    
    //close read fifo
    close(FIFO_R);
    //unlink(readFIFO);
    
    //close write fifo
    close(FIFO_W);
    //unlink(writeFIFO);
    //fprintf(stderr,"IN PLAYER %s host %d, player exit\n",player_index,host_id);
    exit(0);
   
}
