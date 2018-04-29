/*B05902102 黃麗璿*/
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include<errno.h>        /* for definition of errno */
#include<stdarg.h>
#include<time.h>
#include<fcntl.h>
#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct Member {
    int id;
    int index;
    int score;
    int rank;
    int money_pay;
}Member;

int compare1(const void *a, const void *b){
    Member *ma = (Member *)a;
    Member *mb = (Member *)b;
    
    if(ma->money_pay > mb->money_pay){
        return -1;
    }else if(ma->money_pay < mb->money_pay){
        return 1;
    }
    else return 0;
}

int compare2(const void *a, const void *b){
    Member *ma = (Member *)a;
    Member *mb = (Member *)b;
    
    if(ma->id < mb->id){
        return -1;
    }else if(ma->id > mb->id){
        return 1;
    }
    else return 0;
}

int compare3(const void *a, const void *b){
    Member *ma = (Member *)a;
    Member *mb = (Member *)b;
    
    if(ma->score > mb->score){
        return -1;
    }else if(ma->score > mb->score){
        return 1;
    }
    else return 0;
}

/*./host [host_id] */

int main(int argc, char *argv[]){
    
    //if(argc != 2) fprintf(stderr, "No sufficient argument in host.c\n");
    
    int host_id = atoi(argv[1]);
    char playerfifo[4][1024], playerindex[4][2], random_key[4][10] = {0};
    char hostfifo[1024];
    char message[1024];
    Member player[4];
    int n,idp[4];
    pid_t pid[4];
    
    //fprintf(stderr,"IN HOST %d, Welcome to host\n",host_id);
    //handling if there is no message to read :
    if( (n = read(0,message, sizeof(message))) < 0){
        fprintf(stderr,"read error\n");
    }
    else{
        //otherwise,
        message[n-1] = '\0';
        //fprintf(stderr,"IN HOST %d, read from bidding success, message = %s\n",host_id, message);
        //write(1,message,strlen(message));
    }
    sscanf(message,"%d %d %d %d\n",&idp[0],&idp[1],&idp[2],&idp[3]);//get player id
    
    if(idp[0] != -1){
        //prepare host fifo
        sprintf(hostfifo, "host%d.FIFO", host_id);
        
        //preapare player fifo, index, and random key
        srand(time(NULL));
        for(int i = 0; i < 4; i++) {
            sprintf(playerfifo[i], "host%d_%c.FIFO", host_id, 'A'+i);
            
            if(i == 0) strcpy(playerindex[i],"A");
            else if(i == 1) strcpy(playerindex[i],"B");
            else if(i == 2) strcpy(playerindex[i],"C");
            else strcpy(playerindex[i],"D");
            
            playerindex[i][strlen(playerindex[i])] = '\0';
            sprintf(random_key[i],"%d",rand()%65536);//generate random key from 0 to 65536
        }
    }
    //while there are 4 players :
    while(idp[0] != -1 && idp[1] != -1 && idp[2] != -1 && idp[3] != -1){
        //fprintf(stderr,"IN HOST %d, player id = %d %d %d %d\n",host_id,idp[0],idp[1],idp[2],idp[3]);
        int fdh,fdp[4],money[4];
        
        //fprintf(stderr,"reach haasasere\n");
        n = mkfifo(hostfifo,0666);
        if((fdh = open(hostfifo,O_RDWR)) < 0){
            perror("open fifo error");
        }
        //fprintf(stderr,"fd hostfifo = %d\n",fdh);
        
        //open fifo player, fork and execute player
        for(int i = 0; i < 4; i++) {
            n = mkfifo(playerfifo[i],0666);
            fdp[i] = open(playerfifo[i],O_RDWR);
            //fprintf(stderr,"IN HOST, fd player = %d\n",fdp[i]);
            
            player[i].id = idp[i];
            player[i].index = i;
            player[i].score = 0;
            if((pid[i] = fork()) == 0){
                //fprintf(stderr,"IN HOST %d, player %d done, argv = %s %s %s\n",host_id,i+1,argv[1],playerindex[i],random_key[i]);
                
                execl("./player","player",argv[1],playerindex[i],random_key[i],NULL);
            }
            else{
                money[i] = 1000;
            }
        }

        int round = 0;
        char message1[1024],message2[1024],t1;
        int score[4];
        int t2,t3;
        sleep(1);//ensure all player has been executed
        //do ten round competition
        while(round < 10){
            //fprintf(stderr,"\nIN HOST %d,round = %d\n",host_id,round);
            
            //prepare message contains money information to all players
            memset(message1,0,sizeof(message1));
            sprintf(message1,"%d %d %d %d\n",money[0],money[1],money[2],money[3]);
            //fprintf(stderr,"IN HOST %d, message to player = %s",host_id,message1);
            
            //send message to player and read response from player each turn
            for(int i = 0; i < 4; i++){
                //send message to player
                n = write(fdp[i],message1,strlen(message1));
                //fprintf(stderr,"IN HOST %d, write %d bytes to player %s\n",host_id,n,playerindex[i]);
                fsync(fdp[i]);
                
                //read response from player
                memset(message2,0,sizeof(message2));
                if( (n = read(fdh,message2,sizeof(message2))) < 0){
                    perror("IN HOST, read from player error\n");
                }
                else{
                    message2[n-1] = '\0';
                    //fprintf(stderr,"IN HOST %d, message from player %s= %s\n",host_id,playerindex[i],message2);
                    
                    sscanf(message2,"%c %d %d",&t1,&t2,&t3); //t1 player index t2 random key t3 money
                    //the handling part when the random money is not the same and when the money to pay is not sufficient :
                    if(t2 != atoi(random_key[i]) || t3 > money[i]) {
                        //fprintf(stderr, "random key is wrong\n");
                        player[i].money_pay = 0;
                    } else player[i].money_pay = t3;
                }
                
                //each player's money increase by 1000 in the next round
                money[i] += 1000;
            }
            
            //sort struct based on money_pay
            qsort(player,4,sizeof(Member),compare1);
            
            //decide who is the winner of this round
            for(int i = 0; i < 4; i++){
                if(player[i].money_pay != player[i+1].money_pay){
                    player[i].score++;
                    money[player[i].index] -= player[i].money_pay;
                    break;
                }
            }
            
            //sort struct back to original order based on id
            qsort(player,4,sizeof(Member),compare2);
            
            round++;
            //fprintf(stderr,"IN HOST %d, end of round = %d\n",host_id,round);
        }
        
        close(fdh);
        
        //wait all children to exit
        for(int i = 0; i < 4; i++){
            //fprintf(stderr,"IN HOST %d, wait for player %s exit\n",host_id,playerindex[i]);
            waitpid(pid[i],0,0);
            close(fdp[i]);
        }
        
        //sort struct based on score
        qsort(player,4,sizeof(Member),compare3);
        
        //decided rank of each player
        for(int i = 0; i < 4; i++){
            if(i != 0 && player[i].score == player[i-1].score) player[i].rank = player[i-1].rank;
            else player[i].rank = i+1;
        }
        
        //sort struct back to original order based on id
        qsort(player,4,sizeof(Member),compare2);
        
        //send competition result to bidding system
        memset(message,0,sizeof(message));
        sprintf(message,"%d %d\n%d %d\n%d %d\n%d %d\n",
                player[0].id,player[0].rank,
                player[1].id,player[1].rank,
                player[2].id,player[2].rank,
                player[3].id,player[3].rank);
        
        write(1,message,strlen(message));
        fsync(1);
        
        //fprintf(stderr,"IN HOST %d, send result done, waiting for new player assigned\n",host_id);
        
        //unlink all fifo
        unlink(hostfifo);
        for(int i = 0; i < 4; i++)
            unlink(playerfifo[i]);
        
        //waiting new player from bidding system
        memset(message,0,sizeof(message));
        if( (n = read(0,message, sizeof(message))) < 0){
            //fprintf(stderr,"IN HOST, read error\n");
        }
        else{
            message[n-1] = '\0';
            //fprintf(stderr,"IN HOST %d, read success message = %s\n",host_id,message);
        }
        sscanf(message,"%d %d %d %d\n",&idp[0],&idp[1],&idp[2],&idp[3]);
    }

    //fprintf(stderr,"IN HOST %d, host exit\n",host_id);
    exit(0);
}

