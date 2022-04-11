#include<unistd.h>
#include<sys/types.h>
#include<sys/resource.h>
#include<stdio.h>

int main() {
    uid_t uid = getuid();
    uid_t euid = getuid();

    printf("userid is: %d,effective userid is: %d\n",uid,euid);
    
    struct rlimit cur_resoure;
    int ret = getrlimit(RLIMIT_NPROC,&cur_resoure);
    printf("rlim_cur is : %ld, rlim_max is : %ld\n",cur_resoure.rlim_cur,cur_resoure.rlim_max);
}