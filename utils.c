#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "utils.h"

#define LOCK_FILE "/var/lock/myble.lock"
static int fd;
static struct flock lock = {0};

uint8_t ucUtilsOpenLockFile(void)
{
    int res;
    struct flock lock = {0};

    if((fd = open(LOCK_FILE, O_RDWR, S_IRUSR | S_IWUSR)) == -1)
    {
        fd = open(LOCK_FILE, O_CREAT, S_IRUSR | S_IWUSR);
    }
    if(fd == -1)
    {
        printf("file open failed!\n");
        return 1;
    }

    if(-1 == fcntl(fd,F_GETLK, &lock))
    {
        perror ( "Analyzing lock failure");
        return 2;
    }
    if(lock.l_type != F_UNLCK)
    {
        printf("fail:the file has locked!\n");
        return 3;
    }
    else
    {
        memset(&lock,0,sizeof(struct flock));
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 10;
        lock.l_type = F_WRLCK | F_RDLCK;
    }
    if(-1 == fcntl(fd,F_SETLK,&lock))
    {
        perror ( "lock failed");
        return 4;
    }

    return 0;
}

uint8_t ucUtilsCloseUnlockFile(void)
{
    lock.l_type = F_UNLCK;
    if(-1 == fcntl(fd,F_SETLK,&lock))
    {
        perror ( "unsuccessful");
        return 1;
    }

    close(fd);
    return 0;

}

#ifdef TEST_MAIN
int main(int argc, char **argv)
{
    if( 0 != usUtilsOpenLockFile() )
        return 1;
    sleep(10);
    usUtilsCloseUnlockFile();

    return 0;
}
#endif
