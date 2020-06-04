#include "VirtualMachine.h"
#include <stdio.h>
#include <fcntl.h>

void VMMain(int argc, char *argv[]){
    VMPrint("In VMMain:\n");

    int fd;
    // O_CREAT | O_TRUNC | O_RDWR
    // O_RDONLY


    TVMStatus res = VMFileOpen("tmp.txt", O_CREAT | O_TRUNC | O_RDWR, 0, &fd);
    printf("0 Successfully Opened: %d\n", res == VM_STATUS_SUCCESS);
    //
    // res = VMFileOpen("makefile", O_CREAT | O_TRUNC | O_RDWR, 0, &fd);
    // printf("1 Successfully Opened: %d\n", res == VM_STATUS_SUCCESS);

    //
    // res = VMFileOpen("./tmp.txt", flags, O_CREAT | O_TRUNC, &fd);
    // printf("1 Successfully Opened: %d\n", res == VM_STATUS_SUCCESS);
    //
    // res = VMFileOpen("./one/../tmp.txt", O_RDWR, mode, &fd);
    // printf("2 Successfully Opened: %d\n", res == VM_STATUS_SUCCESS);



    // int fd = -1;
    // TVMStatus res = VMDirectoryOpen("/src", &fd);
    // if (res != VM_STATUS_SUCCESS)
    // {
    //   printf("UNSUCCESSFUL OPEN\n");
    // }
    //
    // SVMDirectoryEntry tmp;
    // while(VM_STATUS_SUCCESS == VMDirectoryRead(fd, &tmp)){
    //   printf(">> %s\n", tmp.DShortFileName);
    // }
    //
    //
    // res = VMDirectoryClose(fd);
    // if (res != VM_STATUS_SUCCESS)
    // {
    //   printf("UNSUCCESSFUL CLOSE\n");
    // }


    // printf(">> DONE!!\n");
    // char path_1[512] = { '/', 't', 'e', 'n', '/', 'e', 'l', 'e', 'v', 'e', 'n'};
    // char path_2[512] = { '.', '.', '/', '.', '/', '.', '.', '/', 'n', 'e', 'w' };
    // char path_3[512] = { '/', 's', 'r', 'c', '/', 'o', 'n', 'e', '/', 't', 'w', '0', '.', 't', 'x', 't'};
    // char buffer[512];
    //
    // // int write_fd = 100;
    // int fake_fd = 100;
    // printf("size = %d\n", sizeof(O_RDWR));
    // printf("O_RDONLY = %d\n", O_RDONLY);
    // printf("O_RDWR RES = %d\n", O_RDWR);
    // printf("O_WRONLY RES = %d\n", O_WRONLY);
    // printf("O_TRUNC = %d\n", O_TRUNC);
    // printf("O_CREAT RES = %d\n", O_CREAT);
    //
    // VMFileOpen(path_1, O_RDWR | O_TRUNC | O_CREAT, 0, &fake_fd);
    // VMFileOpen(path_2, 0, 0, &fake_fd);

    // printf("RDONLY: %d\n",O_RDONLY);
    //
    // TVMStatus res = VMFileOpen(path_3, O_RDWR | O_TRUNC, 0, &write_fd);
    // printf("Created RES = %d\n", res);
    //
    // char tmp[3] = {'\n', '!', '\n'};
    // int tmp_l = 3;
    //
    // int bytes_to_write = 11;
    // res = VMFileWrite(write_fd, (void*) tmp, &tmp_l);
    // printf("Wrote RES = %d\n", res);
    // printf("WROTE BYTES = %d\n", tmp_l);
    //
    // VMFileClose(write_fd);
    // //
    // int read_fd = -1;
    // res = VMFileOpen(path_3, O_RDWR | O_CREAT, 0644, &read_fd);
    // printf("Created RES = %d\n", res);
    //
    // int bytes_to_read = 11;
    // res = VMFileRead(read_fd, (void*) buffer, &bytes_to_read);
    // printf("READ RES = %d\n", res);
    // printf("READ BYTES = %d\n", bytes_to_write);
    // printf("READ RES = '%s'\n", buffer);
    //
    //
    // VMFileClose(read_fd);

    // VMDirectoryCurrent(path);
    // VMPrint(path);
    //
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
