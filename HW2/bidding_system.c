/*B05902102 黃麗璿*/
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<limits.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<errno.h>		/* for definition of errno */
#include<stdarg.h>
#define ERR_EXIT(a) { perror(a); exit(1); }

char rounds[100000][32];
int total_rounds = 0;
typedef struct Member {
    int id;
    int rank;
    int score;
}Member;

/*int generating_combinations(int x) {
    int f = 1;
    while(x > 0) {
        f = f * x;
        x--;
    }
    return f;
}*/

void the_set_of_combination(int a[], int data[], int init, int end, int idx) {
    if(idx == 4) {
        sprintf(rounds[total_rounds], "%d %d %d %d\n", data[0], data[1], data[2], data[3]);
        total_rounds++;
        return;
    }
    for(int i = init; i <= end && end-i+1 >= (4-idx); i++) {
        data[idx] = a[i];
        the_set_of_combination(a, data, i+1, end, idx+1);
    }
}

void combinations(int a[], int n) {
    int data[4];
    the_set_of_combination(a, data, 0, n-1, 0);
}

int compare1(const void *a, const void *b){
    Member *ma = (Member *)a;
    Member *mb = (Member *)b;
    
    if(ma->score > mb->score){
        return -1;
    }else if(ma->score < mb->score){
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
/*void insertion_sort(Member Score[], int n) {
    int key;
    Member tmp;
    for(int i = 0; i < n; i++) {
        key = Score[i].score;
        tmp = Score[i];
        int j = i-1;
        while(j >= 0 && Score[j].score > key) {
            Score[j+1] = Score[j];
            j = j-1;
        }
        Score[j+1] = tmp;
    }
    
}*/
/*./bidding_system [host_num] [player_num] */

int main(int argc, char *argv[]){
	int host_num = atoi(argv[1]);
	int player_num = atoi(argv[2]);
    
    int pfd1[host_num][2],pfd2[host_num][2];
    fd_set masterfdw, masterfdr;
    pid_t pid[host_num];
    const char message[] = "-1 -1 -1 -1\n";
    int status;
    
    int a[32], maxfd_r = INT_MIN, maxfd_w = INT_MIN;
    for(int i = 0; i < player_num; i++) a[i] = i+1;
    
    combinations(a, player_num);
    
//    for(int i = 0; i < total_rounds; i++) {
//        printf("%s",rounds[i]);
//    }
    
    //printf("IN BIDDING, host_num = %d, player_num = %d\n",host_num,player_num);

	for(int i = 0; i < host_num; i++) {
        pipe(pfd1[i]);
        pipe(pfd2[i]);
        
        if((pid[i] = fork()) == 0) {
            //this is the host stuff :
            dup2(pfd2[i][1],1); //tell back the bidding the results
            dup2(pfd1[i][0],0); //receive the commands from bidding
            close(pfd1[i][1]);
            close(pfd2[i][0]);
            
            char host_id[1024];
            sprintf(host_id, "%d", i+1);
            if(execl("./host", "host", host_id, NULL) < 0){
                perror("IN CHILD, execl error\n");
            }
            printf("IN BIDDING, child done\n");
        } else {
            //this is the bidding system stuff :
            //the bidding tells the host
            close(pfd1[i][0]);
            close(pfd2[i][1]);
            
            maxfd_r = (maxfd_r < pfd2[i][0]) ? pfd2[i][0] : maxfd_r;
            //printf("IN BIDDING, parent done\n");
        }
    }
    //sleep(1);
    //combinations :
    int comb = total_rounds;
    //printf("IN BIDDING, comb %d\n", comb);
    
    //assign the competition to host
    int min = (host_num < comb) ? host_num : comb;
    for(int i = 0; i < min; i++){
        write(pfd1[i][1], rounds[i], strlen(rounds[i])); //bidding system tells the hosts about the competition order.
        fsync(pfd1[i][1]);
    }
    
    //I/O Multiplexing :
    fd_set readset, masterset;
    
    int remaining = comb - min; //total competition - min , get the remaining competition.
    
    Member Score[player_num+1];
    
    for(int i = 0; i < player_num; i++) {
        Score[i].id = i+1;
        Score[i].score = 0;
    }
    
    FD_ZERO(&masterset);
    FD_ZERO(&readset);
    
    for(int i = 0; i < host_num; i++)
        FD_SET(pfd2[i][0], &masterset);    //bidding system receives the commands from hosts
    
    int readx = 0;
    char buf[1024];
    Member output[4];
    
    while(readx < comb) {
        //fprintf(stderr,"IN BIDDING, readx = %d, comb = %d, remaining = %d\n",readx,comb,remaining);
        readset = masterset;
        select(maxfd_r+1, &readset, NULL, NULL, NULL);
        for(int i = 0; i < host_num; i++) {
            if(FD_ISSET(pfd2[i][0], &readset)) {    //if the pfd is in the readset or READY
                //printf("IN BIDDING, host %d done\n",i+1);
                
                memset(buf,0,sizeof(buf));
                read(pfd2[i][0],buf,sizeof(buf));
                //printf("IN BIDDING, message from host %d = \n%s",i+1,buf);
                sscanf(buf, "%d %d %d %d %d %d %d %d",
                       &output[0].id, &output[0].rank,
                       &output[1].id, &output[1].rank,
                       &output[2].id, &output[2].rank,
                       &output[3].id, &output[3].rank);
                for(int h = 0; h < 4; h++)
                    Score[output[h].id-1].score += (4 - output[h].rank);
                
                readx++;
                //printf("IN BIDDING, start to send new competition to host %d\n",i+1);
                //assign new competition to host
                if(remaining > 0) {
                    //printf("IN BIDDING, send new competition to host %d, message = %s\n",i+1,rounds[comb-remaining]);
                    write(pfd1[i][1], rounds[comb-remaining], strlen(rounds[comb-remaining]));
                    remaining--;
                    fsync(pfd1[i][1]);
                }
            }   //end of if FD_ISSET
        } // end of for loop i
        //readset = masterset;
    }   //end of while
    
    
    //Terminate all the hosts :
    for(int i = 0; i < host_num; i++) {
        write(pfd1[i][1], message, strlen(message)); //bidding system tells the hosts to stop
        fsync(pfd1[i][1]);
        close(pfd1[i][1]);
        close(pfd2[i][0]);
        wait(NULL);
    }
  
    //Sort the result :
    //insertion_sort(Score,player_num);
    
    /*int ranking[player_num+1];
    int former_score = -1, former_rank = -1;
    for(int i = 0; i < player_num; i++) {
        if(former_score == Score[i].score)
            ranking[(Score[i].id)-1] = former_rank;
        else {
            ranking[(Score[i+1].id)-1] = i+1;
            former_rank = i+1;
        }
        former_score = Score[i].score;
    }*/
    
    qsort(Score,player_num,sizeof(Member),compare1);
    
    for(int i = 0; i < player_num; i++){
        if(i != 0 && Score[i].score == Score[i-1].score) Score[i].rank = Score[i-1].rank;
        else Score[i].rank = i+1;
    }
    
    qsort(Score,player_num,sizeof(Member),compare2);
    for(int i = 0; i < player_num; i++)
        printf("%d %d\n", i+1, Score[i].rank);
    
    exit(0);
}
        
