
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <time.h>
#include <ctype.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP_DEFAULT "127.0.0.1"
#define SERVER_PORT_DEFAULT 8080


static void hex_append_byte(char *out, size_t out_size, uint8_t b) {
    const char *hex = "0123456789ABCDEF";
    size_t len = strlen(out);
    if (len + 3 >= out_size) return;
    out[len] = hex[(b >> 4) & 0xF];
    out[len+1] = hex[b & 0xF];
    out[len+2] = '\0';
}
static void bytes_to_hex(const unsigned char *data, size_t n, char *out, size_t out_size) {
    out[0]='\0';
    for (size_t i=0;i<n;i++) hex_append_byte(out,out_size,data[i]);
}

/* ---------- parity ---------- */
int parity_bit_of_text(const char *text) {
    int ones = 0;
    for (size_t i=0;i<strlen(text);i++){
        unsigned char c = (unsigned char)text[i];
        for (int b=0;b<8;b++) if ((c>>b)&1) ones++;
    }
    return ones % 2;
}

/* ---------- 2D parity ---------- */
void compute_2d_parity(const unsigned char *data, size_t len, char *out, size_t out_size) {
    if (len == 0) { snprintf(out,out_size,"rows=;cols=00"); return; }
    char rows[2048]; rows[0]=0;
    for (size_t i=0;i<len;i++){
        int ones=0;
        for (int b=0;b<8;b++) if ((data[i]>>b)&1) ones++;
        size_t l=strlen(rows);
        rows[l] = (ones%2)?'1':'0';
        rows[l+1]=0;
    }
    
    unsigned char col=0;
    for (int b=0;b<8;b++){
        int ones=0;
        for (size_t i=0;i<len;i++) if ((data[i]>>b)&1) ones++;
        if (ones%2) col |= (1<<b);
    }
    char cols_hex[4]={0}; hex_append_byte(cols_hex,sizeof(cols_hex),col);
    snprintf(out,out_size,"rows=%s;cols=%s", rows, cols_hex);
}

/* ---------- checksum ---------- */
uint16_t internet_checksum(const unsigned char *data, size_t len) {
    unsigned long sum = 0;
    size_t i=0;
    while (i+1 < len) {
        uint16_t w = ((uint16_t)data[i]<<8) | (uint16_t)data[i+1];
        sum += w;
        if (sum > 0xFFFF) sum = (sum & 0xFFFF) + (sum >> 16);
        i += 2;
    }
    if (i < len) {
        uint16_t w = ((uint16_t)data[i]<<8);
        sum += w;
        if (sum > 0xFFFF) sum = (sum & 0xFFFF) + (sum >> 16);
    }
    sum = ~sum & 0xFFFF;
    return (uint16_t)sum;
}

/* ---------- CRC---------- */
uint8_t crc8(const unsigned char *data, size_t len) {
    uint8_t crc = 0x00;
    const uint8_t poly = 0x07;
    for (size_t i=0;i<len;i++){
        crc ^= data[i];
        for (int j=0;j<8;j++) {
            if (crc & 0x80) crc = (crc<<1) ^ poly;
            else crc <<= 1;
        }
    }
    return crc;
}
uint16_t crc16_ccitt(const unsigned char *data, size_t len) {
    uint16_t crc = 0xFFFF;
    uint16_t poly = 0x1021;
    for (size_t i=0;i<len;i++){
        crc ^= (uint16_t)data[i] << 8;
        for (int j=0;j<8;j++){
            if (crc & 0x8000) crc = (crc<<1) ^ poly;
            else crc <<= 1;
        }
    }
    return crc & 0xFFFF;
}
uint32_t crc32_ieee(const unsigned char *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    uint32_t poly = 0x04C11DB7;
    for (size_t i=0;i<len;i++){
        crc ^= ((uint32_t)data[i]) << 24;
        for (int j=0;j<8;j++){
            if (crc & 0x80000000) crc = (crc<<1) ^ poly;
            else crc <<= 1;
        }
    }
    return ~crc;
}

/* ---------- HAMMING---------- */
uint8_t hamming74_encode_nibble(uint8_t nibble) {
    int d1=(nibble>>3)&1, d2=(nibble>>2)&1, d3=(nibble>>1)&1, d4=(nibble>>0)&1;
    int p1 = (d1 ^ d2 ^ d4) &1;
    int p2 = (d1 ^ d3 ^ d4) &1;
    int p3 = (d2 ^ d3 ^ d4) &1;
    uint8_t code = (p1<<6) | (p2<<5) | (d1<<4) | (p3<<3) | (d2<<2) | (d3<<1) | d4;
    return code;
}
void hamming74_encode_message(const unsigned char *data, size_t len, unsigned char *out, size_t *out_len) {
    size_t oi=0;
    for (size_t i=0;i<len;i++){
        uint8_t b=data[i];
        uint8_t hi=(b>>4)&0xF, lo=b&0xF;
        out[oi++]=hamming74_encode_nibble(hi);
        out[oi++]=hamming74_encode_nibble(lo);
    }
    *out_len=oi;
}


void print_methods() {
    printf("Choose control method:\n");
    printf("1) PARITY (even/odd)\n");
    printf("2) 2D PARITY\n");
    printf("3) CRC-8\n");
    printf("4) CRC-16 (CCITT)\n");
    printf("5) CRC-32\n");
    printf("6) HAMMING (7,4 per nibble)\n");
    printf("7) INTERNET CHECKSUM\n");
}


int main() {
    char server_ip[64];
    char portline[32];
    int server_port;
    char text[2048];

    printf("=== Client1 (Data Sender) - ALL METHODS IMPLEMENTED ===\n");
    printf("Server IP (default %s): ", SERVER_IP_DEFAULT);
    if (!fgets(server_ip,sizeof(server_ip),stdin)) return 0;
    if (server_ip[0]=='\n') strcpy(server_ip, SERVER_IP_DEFAULT);
    else server_ip[strcspn(server_ip,"\n")]=0;

    printf("Server port (default %d): ", SERVER_PORT_DEFAULT);
    if (!fgets(portline,sizeof(portline),stdin)) return 0;
    if (portline[0]=='\n') server_port = SERVER_PORT_DEFAULT;
    else server_port = atoi(portline);

    
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed.\n"); return 1;
    }

    while (1) {
        printf("\nEnter text message (or QUIT to exit): ");
        if (!fgets(text,sizeof(text),stdin)) break;
        text[strcspn(text,"\n")] = 0;
        if (strcmp(text,"QUIT")==0) break;

        print_methods();
        printf("Choice (1-7): ");
        int choice;
        if (scanf("%d",&choice)!=1) break;
        while (getchar()!='\n'); // clear

        char method[64]={0};
        char control[4096]={0};

        const unsigned char *bytes = (const unsigned char*)text;
        size_t len = strlen(text);

        switch (choice) {
            case 1: {
                printf("Parity mode: 1) EVEN  2) ODD : ");
                int m; if (scanf("%d",&m)!=1) { m=1; } while (getchar()!='\n');
                int p = parity_bit_of_text(text);
                if (m==1) { strcpy(method,"PARITY-EVEN"); sprintf(control,"%d", (p==0)?0:1); }
                else { strcpy(method,"PARITY-ODD"); sprintf(control,"%d", (p==0)?1:0); }
                break;
            }
            case 2: {
                strcpy(method,"2DPARITY");
                compute_2d_parity(bytes,len,control,sizeof(control));
                break;
            }
            case 3: {
                strcpy(method,"CRC8");
                uint8_t r = crc8(bytes,len);
                char tmp[8]={0}; hex_append_byte(tmp,sizeof(tmp),r);
                strcpy(control,tmp);
                break;
            }
            case 4: {
                strcpy(method,"CRC16");
                uint16_t r = crc16_ccitt(bytes,len);
                sprintf(control,"%04X", r & 0xFFFF);
                break;
            }
            case 5: {
                strcpy(method,"CRC32");
                uint32_t r = crc32_ieee(bytes,len);
                sprintf(control,"%08X", (unsigned int)r);
                break;
            }
            case 6: {
                strcpy(method,"HAMMING");
                unsigned char enc[8192]; size_t enc_len=0;
                hamming74_encode_message(bytes,len,enc,&enc_len);
                bytes_to_hex(enc,enc_len,control,sizeof(control));
                break;
            }
            case 7: {
                strcpy(method,"CHECKSUM");
                uint16_t cs = internet_checksum(bytes,len);
                sprintf(control,"%04X", cs);
                break;
            }
            default:
                printf("Invalid choice.\n"); continue;
        }

        char packet[8192];
        snprintf(packet,sizeof(packet),"%s|%s|%s", text, method, control);
        printf("Packet: %s\n", packet);

        
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv;
        serv.sin_family = AF_INET;
        serv.sin_port = htons(server_port);
        serv.sin_addr.s_addr = inet_addr(server_ip);
        if (connect(s,(struct sockaddr*)&serv,sizeof(serv)) < 0) {
            printf("Connect failed. Is server running?\n");
            closesocket(s);
            continue;
        }
        send(s, packet, (int)strlen(packet), 0);
        printf("Packet sent to server.\n");
        closesocket(s);
    }

    WSACleanup();
    printf("Client1 exiting.\n");
    return 0;
}
