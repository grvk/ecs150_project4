#include "VirtualMachine.h"
#include <fcntl.h>

void VMMain(int argc, char *argv[]){
    VMPrint("In VMMain:\n");

    // char tmp[64];
    // for (int i = 0; i < 64; i++)
    // {
    //   tmp[i] = '\0';
    // }

    // VMDirectoryCurrent(tmp);
    // printf(">> CUR DIR: %s\n", tmp);
    // TVMStatus res = VMDirectoryChange("apps");
    // printf(">> SUCCESS CHANGE: %d\n", res == VM_STATUS_SUCCESS);
    // VMDirectoryCurrent(tmp);
    // printf(">> CUR DIR: %s\n", tmp);

    int fd = -1;
    TVMStatus res = VMFileOpen("NewFile1", O_RDWR, 0644, &fd);
    printf(">> OPENED - Success: %d \n", res == VM_STATUS_SUCCESS);
    printf(">> OPENED - FD: %d \n", fd);


    // char buf[4000];
    // for (int i = 0; i < 4000; i++)
    // {
    //   buf[i] = '\0';
    // }
    //
    // int bytes_to_read = 3000;
    // VMFileRead(fd, buf, &bytes_to_read);
    // printf("<<<<< Read bytes: %d\n", bytes_to_read);
    //
    // int new_bytes_to_read = 100;
    // res = VMFileRead(fd, buf, &new_bytes_to_read);
    // printf(" STATUS: %d \n", res);
    // printf(" new_bytes_to_read: %d \n", new_bytes_to_read);

    //
    // printf("<<<<< Read bytes: %d\n", bytes_to_read);
    // printf("%s\n", buf);
    // i += bytes_to_read;




    // int fd = -1;
    // char fname[32] = {'/', 'A', 'P', 'P', 'S', '/', 'F', 'I', 'L', 'E', '.', 'C', ' ', ' ', ' ', ' ', ' ', '\0'};
    // int res = VMFileOpen(fname, O_RDWR, 0644, &fd);
    // printf(">> RES: %d \n", res == VM_STATUS_SUCCESS);
    //
    // int len = 512;
    // char buf[len + 1];
    // for (int i = 0; i < len + 1; i++)
    // {
    //   buf[i] = '\0';
    // }

    // VMFileRead(fd, buf, &len);
    // //

    // printf("Done\n");


    //
    // int fd_1, fd_2;
    //
    // char fname[13] = {'/', 't', 'm', 'p', '/', 't', 'm', 'p', '.', 't', 'x', 't', '\0'};
    //
    // int res1 = VMFileOpen(fname, O_RDWR, 0644, &fd_1);
    // int res2 = VMFileOpen(fname, O_RDWR, 0644, &fd_2);
    //
    // char buf1[32], buf2[32];
    // for (int i = 0; i < 32; i++)
    // {
    //   buf1[i] = '\0';
    //   buf2[i] = '\0';
    // }
    //
    // int len1 = 2, len2 = 4;
    //
    // printf(">> RES1: %d \n", res1);
    // printf(">> RES2: %d \n", res1);
    //
    // VMFileRead(fd_1, buf1, &len1);
    // VMFileRead(fd_2, buf2, &len2);
    //
    // printf("READ 1: %s\n", buf1);
    // printf("READ 2: %s\n", buf2);
    //
    // for (int i = 0; i < 32; i++)
    // {
    //   buf1[i] = '\0';
    //   buf2[i] = '\0';
    // }
    // len1 = 1;
    // len2 = 2;
    //
    // VMFileRead(fd_1, buf1, &len1);
    // VMFileRead(fd_2, buf2, &len2);
    //
    // printf("[2] READ 1: %s\n", buf1);
    // printf("[2] READ 2: %s\n", buf2);
    //
    // for (int i = 0; i < 32; i++)
    // {
    //   buf1[i] = '\0';
    //   buf2[i] = '\0';
    // }
    //
    // buf2[0] = '~';
    // buf2[1] = '+';
    // buf2[2] = '~';
    // buf2[3] = '!';
    // len2 = 3;
    //
    // VMFileWrite(fd_2, buf2, &len2);
    //
    // len1 = 32;
    // VMFileRead(fd_1, buf1, &len1);
    // printf("[3] READ 1: %s\n", buf1);




    // char path[512];
    // VMDirectoryCurrent(path);
    // VMPrint(path);
    // VMPrint("\n");
    //
    // char new_path[5] = {'A', 'p', 'P', 's', '\0'};
    // VMDirectoryChange(new_path);
    //
    // VMDirectoryCurrent(path);
    // VMPrint(path);
    // VMPrint("\n");


    // path[0] = '/';
    // path[1] = '/';
    // path[2] = '/';
    // path[3] = '/';
    // path[4] = '/';
    // path[5] = '/';
    // path[6] = '/';
    // path[7] = '/';
    // path[8] = '/';
    // path[9] = '/';
    // path[10] = '/';

    VMPrint("Goodbye\n");
}
