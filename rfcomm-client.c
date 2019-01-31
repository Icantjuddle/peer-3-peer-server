#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    struct sockaddr_rc addr = {0};
    int s, status;
    char dest[18] = " 9C:B6:D0:1F:23:32"; //"64:A2:F9:C0:51:56"; // "14:56:8E:FB:DC:40"; //David's macaddr

    // allocate a socket
    s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // set the connection parameters (who to connect to)
    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = (uint8_t)22;
    str2ba(dest, &addr.rc_bdaddr);

    // connect to server
    status = connect(s, (struct sockaddr*)&addr, sizeof(addr));

    // send a message
    if (status == 0)
    {
        status = write(s, "hello!\n", 7);
    }

    if (status < 0) perror("bad");

    close(s);
    return 0;
}
