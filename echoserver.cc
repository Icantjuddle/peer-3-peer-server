#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <cstdio>
#include <fcntl.h>
#include <inttypes.h>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <pthread.h>
#include <regex>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
extern "C"
{
#include "register.h"
}

typedef struct
{
    int accpted_fd;
    pthread_t thread_id;
} thread_args;

void handle_sigint(int signal);
int parse_input(int argc, char** argv);
void* handle_connection(void* args);

int verb_flag = 0;
int abbr_flag = 0;
int do_shutdown = 0;
int sock_opt = 1;
int sockfd = -1;
uint8_t bt_channel = 3;

// COMMANDS
std::regex quit_re("QUIT", std::regex_constants::icase);
std::regex echo_re("ECHO\\s+(.*)", std::regex_constants::icase);

std::map<pthread_t, int> pthread_state;

int main(int argc, char* argv[])
{
    parse_input(argc, argv);
    if (abbr_flag)
    {
        std::cerr << "Author: Ben Judd (benjudd)\n"
                     "Basic Bluetooth server using Bluez Libaray"
                  << std::endl;
        return 1;
    }
    struct sigaction new_sg;
    struct sigaction old_sg;
    new_sg.sa_handler = &handle_sigint;
    new_sg.sa_flags = SA_RESTART;
    sigfillset(&new_sg.sa_mask);
    if (sigaction(SIGINT, &new_sg, &old_sg) < 0)
    {
        std::cerr << "Could not set set up signal handler" << std::endl;
        return -1;
    }

    auto service = register_service(bt_channel);
    sockfd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (sockfd < 0)
    {
        std::cerr << "Error encountered opening socket" << std::endl;
        return -1;
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &sock_opt, sizeof(sock_opt)) <
        0)
    {
        std::cerr << "Could not set socket options" << std::endl;
        close(sockfd);
        return -1;
    }

    struct sockaddr_rc bl_addr = {0};
    bzero(&bl_addr, sizeof(bl_addr));
    bl_addr.rc_family = AF_BLUETOOTH;
    bl_addr.rc_channel = bt_channel;
    str2ba("9C:B6:D0:1F:23:32", &bl_addr.rc_bdaddr);

    if (bind(sockfd, (const sockaddr*)&bl_addr, sizeof(bl_addr)) < 0)
    {
        std::perror("Could not bind socket");
        close(sockfd);
        return -1;
    }
    listen(sockfd, 10);
    int cleanup_counter = 0;
    if (verb_flag)
        std::cerr << "Starting bluetooth server on channel " << std::to_string(bt_channel)
                  << std::endl;
    std::string incoming_mac_addr(17, '-');
    while (1)
    {
        struct sockaddr_rc client = {0};
        socklen_t len = sizeof(client);
        thread_args* args =
            (thread_args*)malloc(sizeof(thread_args)); // Will get freed in the pthread

        args->accpted_fd = accept(sockfd, (struct sockaddr*)&client, &len);
        if (verb_flag)
        {
            ba2str(&client.rc_bdaddr, &incoming_mac_addr[0]);
            std::cerr << "Incoming connection from: " << incoming_mac_addr << std::endl;
        }
        pthread_create(&(args->thread_id), NULL, &handle_connection, (void*)args);
        pthread_state[args->thread_id] = 1;

        if (cleanup_counter == 1) // Cleanup old pthreads every X new connections
        {
            cleanup_counter = 0;
            for (auto it = pthread_state.begin(); it != pthread_state.end();)
            { // Threads are started with this value=1, mark 0 before they exit
                if (it->second == 0)
                {
                    pthread_join(it->first, NULL);
                    it = pthread_state.erase(it);
                }
                else
                    it++;
            }
        }
        cleanup_counter++;
    }
    close(sockfd);
    return 0;
}

void handle_sigint(int signal)
{
    if (signal != SIGINT) return;
    std::cerr << "\nShutting down... " << std::endl;
    do_shutdown = 1;
    while (!pthread_state.empty())
    {
        for (auto it = pthread_state.begin(); it != pthread_state.end();)
        {
            if (it->second == 0)
            {
                pthread_join(it->first, NULL);
                it = pthread_state.erase(it);
            }
            else
                it++;
        }
    }
    exit(0);
}
void* handle_connection(void* args)
{
    thread_args* config = (thread_args*)args;
    int opts;
    if ((opts = fcntl(config->accpted_fd, F_GETFL, 0)) != -1)
        fcntl(config->accpted_fd, F_SETFL, opts | O_NONBLOCK);
    // std::string command;
    std::string buffer;
    int cmd_state = 0; // goes to 1 after <CR>, is reset
    write(config->accpted_fd, "+OK Server ready (Author: Ben Judd (benjudd)\r\n", 46);
    if (verb_flag) fprintf(stderr, "[%3d] New Connection\n", config->accpted_fd);
    char c;
    fd_set readset;
    struct timeval timeout;
    while (do_shutdown == 0)
    {
        FD_ZERO(&readset);
        FD_SET(config->accpted_fd, &readset);
        timeout.tv_sec = 3; // 3 second timeout on select for use in shutdown mid-read
        timeout.tv_usec = 0;
        if (select(config->accpted_fd + 1, &readset, NULL, NULL, &timeout) < 0) continue;
        if (read(config->accpted_fd, &c, 1) != 1) continue;
        if (c == '\r' && cmd_state == 0)
        {
            cmd_state = 1;
        }
        else if (c != '\n' || cmd_state == 0)
        {
            if (cmd_state == 1) buffer.push_back('\r');
            cmd_state = 0;
            buffer.push_back(c);
        }
        else // cmd_state = 1 and c = '\n'
        {
            char write_buf[1024];
            int write_len = 0;
            if (verb_flag) fprintf(stderr, "[%3d] S: %s\n", config->accpted_fd, buffer.c_str());
            std::smatch cmd_text;
            if (std::regex_match(buffer, cmd_text, quit_re))
            {
                write(config->accpted_fd, "+OK Goodbye!\r\n", 14);
                break;
            }
            else if (std::regex_match(buffer, cmd_text, echo_re))
            {
                if (cmd_text.size() == 2)
                    write_len = sprintf(write_buf, "+OK %s\r\n", cmd_text[1].str().c_str());
            }
            else
            {
                write_len = sprintf(write_buf, "-ERR Unknown command\r\n");
            }
            if (write_len > 0)
            {
                write(config->accpted_fd, write_buf, write_len);
                if (verb_flag) fprintf(stderr, "[%3d] C: %s", config->accpted_fd, write_buf);
            }
            buffer.clear(); // Reset for next command
            cmd_state = 0;
        }
    }
    if (verb_flag) fprintf(stderr, "[%3d] Connection Closed\r\n", config->accpted_fd);
    pthread_state[config->thread_id] = 0;
    close(config->accpted_fd);
    free(config);
    pthread_exit(NULL);
}

int parse_input(int argc, char** argv)
{
    opterr = 1;
    int ret_val = 0;
    char c;
    while ((c = getopt(argc, argv, "avhc:")) >= 0)
    {
        switch (c)
        {
        case 'h':
            std::cerr << "Options\n"
                         "\t \"-a\" prints out author information\n"
                         "\t \"-v\" outputs verbose debugging info\n"
                         "\t \"-c <n>\" specificy which channel to listen on \n"
                      << std::endl;
            exit(0);
        case 'a':
            abbr_flag = 1;
            break;
        case 'v':
            verb_flag = 1;
            break;
        case 'c':
        {
            int ret = sscanf(optarg, "%" SCNd8, &bt_channel);
            if (bt_channel == 0 || ret < 0)
            {
                std::cerr << "invalid number of bt channel \"" << optarg << "\"" << std::endl;
                return -1;
            }
        }
        break;
        case '?':
            ret_val = -1;
            if (optopt == 'c')
                std::cerr << "Option \"c\" requires an argument\n";
            else
                std::cerr << "Unknown option \"" << optopt << "\"\n";
            break;
        default:
            std::cerr << "Error parsing input\n";
        }
    }
    return ret_val;
}
