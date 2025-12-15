

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <time.h>
#include <ctype.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUF_SIZE 8192


char random_printable_char() {
    int r = rand() % (126 - 33 + 1) + 33;
    return (char)r;
}



/* Bit Flip */
void error_bit_flip(char *data) {
    size_t len = strlen(data);
    if (len==0) return;
    size_t pos = rand() % len;
    int bit = rand() % 8;
    data[pos] ^= (1 << bit);
}

/* Character Substitution */
void error_char_sub(char *data) {
    size_t len = strlen(data);
    if (len==0) return;
    size_t pos = rand() % len;
    data[pos] = random_printable_char();
}

/* Character Deletion */
void error_char_delete(char *data) {
    size_t len = strlen(data);
    if (len<=1) { data[0]=0; return; }
    size_t pos = rand() % len;
    for (size_t i=pos;i<len;i++) data[i]=data[i+1];
}

/* Random Character Insertion */
void error_char_insert(char *data) {
    size_t len = strlen(data);
    if (len + 2 >= BUF_SIZE) return;
    size_t pos = rand() % (len+1);
    char c = random_printable_char();
    for (size_t i=len+1;i>pos;i--) data[i]=data[i-1];
    data[pos]=c;
}

/* Character Swapping */
void error_char_swap(char *data) {
    size_t len = strlen(data);
    if (len<2) return;
    size_t pos = rand() % (len-1);
    char tmp = data[pos]; data[pos]=data[pos+1]; data[pos+1]=tmp;
}

/* Multiple Bit Flips  */
void error_multi_bitflip(char *data) {
    size_t len = strlen(data);
    if (len==0) return;
    int flips = 1 + rand()%5; 
    for (int f=0; f<flips; f++) {
        size_t pos = rand() % len;
        int bit = rand() % 8;
        data[pos] ^= (1<<bit);
    }
}

/* Burst Error */
void error_burst(char *data) {
    size_t len = strlen(data);
    if (len==0) return;
    int burst = 3 + rand()%6; // 3-8
    if ((int)len < burst) burst = (int)len;
    size_t start = rand() % (len - burst + 1);
    for (int i=0;i<burst;i++) {
        
        if (rand()%2) data[start+i] = random_printable_char();
        else { int bit = rand()%8; data[start+i] ^= (1<<bit); }
    }
}


void apply_corruption(char *data, int mode) {
    if (mode==0) mode = 1 + (rand()%7); 
    switch (mode) {
        case 1: error_bit_flip(data); break;
        case 2: error_char_sub(data); break;
        case 3: error_char_delete(data); break;
        case 4: error_char_insert(data); break;
        case 5: error_char_swap(data); break;
        case 6: error_multi_bitflip(data); break;
        case 7: error_burst(data); break;
    }
}

int main() {
    srand((unsigned)time(NULL));
    WSADATA wsa;
    SOCKET listenSock, c1Sock, c2Sock;
    struct sockaddr_in serverAddr, clientAddr;
    int addrlen = sizeof(clientAddr);
    char buf[BUF_SIZE];

    printf("Starting server (Data Corruptor)\n");
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { printf("WSA startup error\n"); return 1; }

    listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock == INVALID_SOCKET) { printf("socket failed\n"); return 1; }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(listenSock,(struct sockaddr*)&serverAddr,sizeof(serverAddr))==SOCKET_ERROR) {
        printf("bind failed\n"); closesocket(listenSock); WSACleanup(); return 1;
    }

    listen(listenSock, 5);
    printf("Server listening on port %d\n", PORT);

   
    printf("Choose corruption mode:\n");
    printf(" 0) RANDOM each packet\n");
    printf(" 1) Bit flip (single)\n");
    printf(" 2) Character substitution\n");
    printf(" 3) Character deletion\n");
    printf(" 4) Random insertion\n");
    printf(" 5) Character swap\n");
    printf(" 6) Multiple bit flips\n");
    printf(" 7) Burst error (3-8 chars)\n");
    printf("Enter mode (0-7, default 0): ");
    int mode = 0;
    char t[8];
    if (!fgets(t,sizeof(t),stdin)) {}
    if (t[0] != '\n') mode = atoi(t);

    printf("Waiting for Client 1...\n");
    c1Sock = accept(listenSock,(struct sockaddr*)&clientAddr,&addrlen);
    if (c1Sock == INVALID_SOCKET) { printf("accept c1 failed\n"); return 1; }
    printf("Client 1 connected.\n");

    printf("Waiting for Client 2...\n");
    c2Sock = accept(listenSock,(struct sockaddr*)&clientAddr,&addrlen);
    if (c2Sock == INVALID_SOCKET) { printf("accept c2 failed\n"); return 1; }
    printf("Client 2 connected.\n");

    
    while (1) {
        int r = recv(c1Sock, buf, sizeof(buf)-1, 0);
        if (r <= 0) { printf("Client1 disconnected or recv error\n"); break; }
        buf[r]=0;
        printf("Received from Client1: %s\n", buf);

       
        char data[BUF_SIZE]; char method[256]; char control[2048];
        data[0]=method[0]=control[0]=0;
        char *p1 = strchr(buf,'|');
        char *p2 = NULL;
        if (p1) p2 = strchr(p1+1,'|');
        if (!p1 || !p2) {
            printf("Invalid packet format; forwarding unchanged.\n");
            send(c2Sock, buf, r, 0);
            continue;
        }
       
        size_t data_len = p1 - buf;
        strncpy(data, buf, data_len); data[data_len]=0;
        size_t method_len = p2 - (p1+1);
        strncpy(method, p1+1, method_len); method[method_len]=0;
        strcpy(control, p2+1);

        printf("Parsed -> DATA: '%s'  METHOD: '%s'  CONTROL: '%s'\n", data, method, control);

        
        char corrupted[BUF_SIZE];
        strncpy(corrupted, data, sizeof(corrupted)-1); corrupted[sizeof(corrupted)-1]=0;

        apply_corruption(corrupted, mode);

      
        char newpacket[BUF_SIZE];
        snprintf(newpacket, sizeof(newpacket), "%s|%s|%s", corrupted, method, control);

        printf("Corrupted DATA -> '%s'\n", corrupted);
        printf("Forwarding packet: %s\n", newpacket);

        send(c2Sock, newpacket, (int)strlen(newpacket), 0);
    }

    closesocket(c1Sock); closesocket(c2Sock); closesocket(listenSock);
    WSACleanup();
    return 0;
}
