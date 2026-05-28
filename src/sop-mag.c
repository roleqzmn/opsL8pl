#include "l8_common.h"

#define SPELL_TYPES 3
const char* spell_names[SPELL_TYPES] = {"Divination", "Summon Elemental", "Fireball"};
const int spell_costs[SPELL_TYPES] = {1, 3, 4};
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

typedef struct{
    char name[MAX_NAME_LENGTH+1];
    struct sockaddr addr;
    int pebbles;
}user_t;

typedef struct{
    int16_t spell;
    int16_t x;
    int16_t y;
    user_t* caster;
} cast_msg_up_t;



typedef struct queue_node_t {
    cast_msg_up_t msg;
    struct queue_node_t* next;
} queue_node_t;

typedef struct{
    queue_node_t* head;
    queue_node_t* tail;
    int count;
} msg_queue_t;

typedef struct{
    pthread_mutex_t* mutex;
    int serverfd;
    sem_t* queue_sem;
    msg_queue_t* queue;
} thread_args_t;

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}
void handle_msg(pthread_mutex_t* mutex, sem_t* queue_sem, int serverfd, msg_queue_t* queue){
    int count = 0;
    msg_t buffer;
    user_t user1;
    user1.pebbles = 10;
    user_t user2;
    user2.pebbles = 10;

    while(count!=2){
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        ssize_t bytes_read = recvfrom(serverfd, &buffer, sizeof(buffer), 0, &client_addr, &client_addr_len);
        if(bytes_read<0)
            ERR("recvfrom");
        if(bytes_read!=sizeof(buffer))
        {
            fprintf(stderr, "Received message of invalid size\n");
            continue;
        }
        if(buffer.type=='l'){
            char name[MAX_NAME_LENGTH+1];
            memcpy(name, buffer.message.name, MAX_NAME_LENGTH);
            name[MAX_NAME_LENGTH]='\0';
            printf("[Login] Welcome, %s!\n", name);
            if(count==0){
                memcpy(user1.name, name, MAX_NAME_LENGTH+1);
                memcpy(&user1.addr, &client_addr, sizeof(struct sockaddr));
            }
            else if(count==1){
                if(memcmp(&user1.addr, &client_addr, sizeof(struct sockaddr))==0)
                {
                    fprintf(stderr, "Received duplicate login message from the same client. Ignoring.\n");
                    continue;
                }
                memcpy(user2.name, name, MAX_NAME_LENGTH+1);
                memcpy(&user2.addr, &client_addr, sizeof(struct sockaddr));
            }
            count++;
        }
        else{
            fprintf(stderr, "Received non-login message while waiting for clients to connect\n");
            continue;
        }
    }
    while(1){
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        ssize_t bytes_read = recvfrom(serverfd, &buffer, sizeof(buffer), 0, &client_addr, &client_addr_len);
        if(bytes_read<0)
            ERR("recvfrom");
        if(bytes_read!=sizeof(buffer))
        {
            fprintf(stderr, "Received message of invalid size\n");
            continue;
        }
        if(memcmp(&user1.addr, &client_addr, sizeof(struct sockaddr))!=0 && memcmp(&user2.addr, &client_addr, sizeof(struct sockaddr))!=0)
        {
            fprintf(stderr, "Received message from unknown client. Ignoring.\n");
            continue;
        }
        if(buffer.type == 'q'){
            if(memcmp(&user1.addr, &client_addr, sizeof(struct sockaddr))==0){
                printf("[Quit] %s quit. Goodbye!\n", user1.name);
                printf("Congratulations, %s! You win!\n", user2.name);
            }
            else{
                printf("[Quit] %s quit. Goodbye!\n", user2.name);
                printf("Congratulations, %s! You win!\n", user1.name);
            }
            printf("[Quit] Someone quit. Goodbye!\n");
        }
        if(buffer.type == 'c'){
            queue_node_t* new_node = malloc(sizeof(queue_node_t));
            if(new_node == NULL)
                ERR("malloc");
            if(memcmp(&user1.addr, &client_addr, sizeof(struct sockaddr))==0)
                new_node->msg.caster = &user1;
            else
                new_node->msg.caster = &user2;
            new_node->msg.spell = ntohs(buffer.message.cast.spell);
            new_node->msg.x = ntohs(buffer.message.cast.x);
            new_node->msg.y = ntohs(buffer.message.cast.y);
            new_node->next = NULL;    
            pthread_mutex_lock(mutex);
            if(queue->count>=MAX_QUEUE)
            {   
                pthread_mutex_unlock(mutex);
                fprintf(stderr, "Queue full. Ignoring cast message\n");
                free(new_node);
                continue;
            }
            if(queue->tail == NULL)
            {
                queue->head = new_node;
                queue->tail = new_node;
            }
            else
            {
                queue->tail->next = new_node;
                queue->tail = new_node;
            }
            queue->count++;
            pthread_mutex_unlock(mutex);
            sem_post(queue_sem);
        }
        else{
            continue;
        }
    }
}

void* familiar_thread(void* args){
    thread_args_t* thread_args = (thread_args_t*)args;
    pthread_mutex_t* mutex = thread_args->mutex;
    sem_t* queue_sem = thread_args->queue_sem;
    msg_queue_t* queue = thread_args->queue;

    while(1){
        sem_wait(queue_sem);
        pthread_mutex_lock(mutex);
        if(queue->count==0)
        {
            pthread_mutex_unlock(mutex);
            continue;
        }
        int16_t spell = queue->head->msg.spell;
        int16_t x = queue->head->msg.x;
        int16_t y = queue->head->msg.y;
        user_t* caster = queue->head->msg.caster;
        
        queue_node_t* temp = queue->head;
        queue->head = queue->head->next;
        free(temp);
        queue->count--;
        if(queue->count == 0)
            queue->tail = NULL;
        if(caster->pebbles < spell_costs[spell])
        {
            fprintf(stderr, "Not enough pebbles to cast spell\n");
            pthread_mutex_unlock(mutex);
            continue;
        }
        caster->pebbles -= spell_costs[spell];
        pthread_mutex_unlock(mutex);

        if(spell<0 || spell>=SPELL_TYPES || x<0 || x>=BOARD_SIZE || y<0 || y>=BOARD_SIZE)
        {
            fprintf(stderr, "Received invalid cast message\n");
            continue;
        }

        ms_sleep(FAMILIAR_DELAY);

        printf("[Cast] %s casts %s onto %d,%d\n", caster->name, spell_names[spell], x, y);
    }
    return NULL;
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

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    msg_queue_t queue = {NULL, NULL, 0};

    sem_t queue_sem;
    if(sem_init(&queue_sem, 0, 0)<0)
        ERR("sem_init");

    thread_args_t args;
    args.mutex = &mutex;
    args.serverfd = serverfd;
    args.queue_sem = &queue_sem;
    args.queue = &queue;

    pthread_t threads[THREAD_COUNT];
    for(int i=0; i<THREAD_COUNT; i++)
    {
        
        if(pthread_create(&threads[i], NULL, familiar_thread, &args)<0)
            ERR("pthread_create");
    }  
    handle_msg(&mutex, &queue_sem, serverfd, &queue);

    pthread_mutex_destroy(&mutex);
    sem_destroy(&queue_sem);
    close(serverfd);
    exit(EXIT_SUCCESS);
}
