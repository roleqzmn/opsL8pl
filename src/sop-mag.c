#include "l8_common.h"

#define SPELL_TYPES 3
const char* spell_names[SPELL_TYPES] = {"Divination", "Summon Elemental", "Fireball"};
#define BOARD_SIZE 8
#define BACKLOG 16

#define MAX_QUEUE 10
#define THREAD_COUNT 3
#define FAMILIAR_DELAY 100

#define MAX_CLIENTS 2
#define MAX_NAME_LENGTH 14

typedef struct __attribute__((__packed__))
{
    int16_t spell;
    int16_t x;
    int16_t y;
} cast_msg_t;

typedef struct __attribute__((__packed__))
{
    char type;
    char padding;
    union 
    {
        char name[MAX_NAME_LENGTH];
        cast_msg_t cast;
    }message;   
} msg_t;

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    if(argc!=2)
    {
         usage(argv[0]);
    }
    int port = atoi(argv[1]);
    if(port<=0)
    {
        usage(argv[0]);
    }

    uint16_t serverfd = bind_inet_socket(port, SOCK_DGRAM, BACKLOG);
    int count=0;
    msg_t buffer;

    while(count<4){
        ssize_t bytes_read = recvfrom(serverfd, &buffer, sizeof(buffer), 0, NULL, NULL);
        if(bytes_read<0)
            ERR("recvfrom");
        if(bytes_read!=sizeof(buffer))
        {
            fprintf(stderr, "Received message of invalid size\n");
            continue;
        }
        if(buffer.type=='l'){
            char name[15];
            memcpy(name, buffer.message.name, MAX_NAME_LENGTH);
            name[MAX_NAME_LENGTH]='\0';
            printf("[Login] Welcome, %s!\n", name);
            count++;
        }
        if(buffer.type == 'q'){
            printf("[Quit] Someone quit. Goodbye!\n");
            count++;
        }
        if(buffer.type == 'c'){
            int16_t spell = ntohs(buffer.message.cast.spell);
            int16_t x = ntohs(buffer.message.cast.x);
            int16_t y = ntohs(buffer.message.cast.y);
            if(spell<0 || spell>=SPELL_TYPES || x<0 || x>=BOARD_SIZE || y<0 || y>=BOARD_SIZE)
            {
                fprintf(stderr, "Received invalid cast message\n");
                continue;
            }
            printf("[Cast] Someone casts %s onto %d,%d\n", spell_names[spell], x, y);
        }
        else{
            continue;
        }
    }
    close(serverfd);
}
