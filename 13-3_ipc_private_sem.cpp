#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

union semun
{
    int val;
    struct semid_ds* buf;
    unsigned short int* array;
    struct seminfo* _buf;
};

/* op 为 -1 时执行 p 操作， op 为1 时执行 v 操作 */
void pv(int sem_id, int op ) {
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op = op;
    sem_b.sem_flg = SEM_UNDO;
    semop( sem_id, &sem_b, 1 );
}

int main( int argc, char* argv[] ) {
    int sem_id = semget( IPC_PRIVATE, 1, 0666 );
    
    union semun sem_un;
    sem_un.val = 1;
    semctl( sem_id, 0, SETVAL, sem_un ); // 设置信号量集中唯一一个信号量的值为 1

    pid_t id = fork();
    if ( id < 0 ) {
        return 1;
    } else if ( id == 0 ) { // 子进程
        printf( "child try to get binary sem\n" );
        /* 由于刚刚的 sem_id 子进程也知道，所以也可以操作该信号量的标识符 */
        pv( sem_id, -1 );
        printf( "child get the sem and would release it after 5 seconds\n" );
        sleep( 5 );
        pv( sem_id, 1 );
        exit( 0 );
    } else {
        printf( "parent try to get binary sem\n" );
        pv (sem_id, -1 );
        printf( "parent get the sem and would release it after 5 seconds\n" );
        sleep(5);
        pv ( sem_id, 1 );
    }
    
    waitpid( id, NULL, 0 );
    semctl( sem_id, 0, IPC_RMID, sem_un ); /* 删除信号量 */
    return 0;
}