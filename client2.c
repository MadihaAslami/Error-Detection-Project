

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP_DEFAULT "127.0.0.1"
#define SERVER_PORT_DEFAULT 8080
#define BUF_SIZE 8192


#ifdef _WIN32
  #define strcasecmp _stricmp
#endif


static void hex_append_byte(char *out, size_t out_size, uint8_t b) {
    const char *hex="0123456789ABCDEF"; size_t len=strlen(out);
    if (len + 3 >= out_size) return;
    out[len] = hex[(b >> 4) & 0xF];
    out[len+1] = hex[b & 0xF];
    out[len+2] = '\0';
}
static void bytes_to_hex(const unsigned char *data, size_t n, char *out, size_t out_size) {
    out[0] = '\0';
    for (size_t i=0;i<n;i++) hex_append_byte(out,out_size,data[i]);
}

/* parity */
int parity_bit_of_text(const char *text) {
    int ones=0;
    for (size_t i=0;i<strlen(text);i++){
        unsigned char c=(unsigned char)text[i];
        for (int b=0;b<8;b++) if ((c>>b)&1) ones++;
    }
    return ones%2;
}

/* 2d parity*/
void compute_2d_parity(const unsigned char *data, size_t len, char *out, size_t out_size) {
    if (len==0) { snprintf(out,out_size,"rows=;cols=00"); return; }
    char rows[2048]; rows[0]=0;
    for (size_t i=0;i<len;i++){
        int ones=0;
        for (int b=0;b<8;b++) if ((data[i]>>b)&1) ones++;
        size_t l=strlen(rows); rows[l]= (ones%2)?'1':'0'; rows[l+1]=0;
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

/* checksum */
uint16_t internet_checksum(const unsigned char *data, size_t len) {
    unsigned long sum=0; size_t i=0;
    while (i+1<len) { uint16_t w = ((uint16_t)data[i]<<8) | (uint16_t)data[i+1]; sum+=w; if (sum>0xFFFF) sum=(sum&0xFFFF)+(sum>>16); i+=2; }
    if (i<len) { uint16_t w = ((uint16_t)data[i]<<8); sum+=w; if (sum>0xFFFF) sum=(sum&0xFFFF)+(sum>>16); }
    sum = ~sum & 0xFFFF; return (uint16_t)sum;
}

/* CRCs */
uint8_t crc8(const unsigned char *data, size_t len) {
    uint8_t crc=0x00; const uint8_t poly=0x07;
    for (size_t i=0;i<len;i++){ crc ^= data[i]; for (int j=0;j<8;j++){ if (crc & 0x80) crc = (crc<<1)^poly; else crc <<=1; } }
    return crc;
}
uint16_t crc16_ccitt(const unsigned char *data, size_t len) {
    uint16_t crc=0xFFFF; uint16_t poly=0x1021;
    for (size_t i=0;i<len;i++){ crc ^= (uint16_t)data[i]<<8; for (int j=0;j<8;j++){ if (crc & 0x8000) crc=(crc<<1)^poly; else crc<<=1; } }
    return crc & 0xFFFF;
}
uint32_t crc32_ieee(const unsigned char *data, size_t len) {
    uint32_t crc=0xFFFFFFFF; uint32_t poly=0x04C11DB7;
    for (size_t i=0;i<len;i++){ crc ^= ((uint32_t)data[i])<<24; for (int j=0;j<8;j++){ if (crc & 0x80000000) crc=(crc<<1)^poly; else crc<<=1; } }
    return ~crc;
}

/* Hamming */
uint8_t hamming74_encode_nibble(uint8_t nibble) {
    int d1=(nibble>>3)&1,d2=(nibble>>2)&1,d3=(nibble>>1)&1,d4=(nibble>>0)&1;
    int p1=(d1^d2^d4)&1, p2=(d1^d3^d4)&1, p3=(d2^d3^d4)&1;
    uint8_t code=(p1<<6)|(p2<<5)|(d1<<4)|(p3<<3)|(d2<<2)|(d3<<1)|d4;
    return code;
}
void hamming74_encode_message(const unsigned char *data, size_t len, unsigned char *out, size_t *out_len) {
    size_t oi=0; for (size_t i=0;i<len;i++){ uint8_t b=data[i]; uint8_t hi=(b>>4)&0xF, lo=b&0xF; out[oi++]=hamming74_encode_nibble(hi); out[oi++]=hamming74_encode_nibble(lo); } *out_len=oi;
}


void recompute_and_compare(const char *data, const char *method, const char *control) {
    printf("Recomputing control for METHOD=%s ...\n", method);
    int equal = 0;

    if (strcasecmp(method,"PARITY-EVEN")==0 || strcasecmp(method,"PARITY-ODD")==0) {
        int p = parity_bit_of_text(data);
        int computed_paritybit = 0;
        if (strcasecmp(method,"PARITY-EVEN")==0) {
            
            computed_paritybit = (p == 0) ? 0 : 1;
        } else {
           
            computed_paritybit = (p == 0) ? 1 : 0;
        }
        int sent = atoi(control);
        equal = (sent == computed_paritybit);
        printf("Sent Check Bits : %s\n", control);
        printf("Computed Check  : %d\n", computed_paritybit);
    }
    else if (strcasecmp(method,"2DPARITY")==0) {
        char calc[2048]; compute_2d_parity((const unsigned char*)data, strlen(data), calc, sizeof(calc));
        printf("Sent Check Bits : %s\n", control);
        printf("Computed Check  : %s\n", calc);
        equal = (strcmp(calc, control) == 0);
    }
    else if (strcasecmp(method,"CRC8")==0) {
        uint8_t r = crc8((const unsigned char*)data, strlen(data));
        char tmp[8]={0}; hex_append_byte(tmp,sizeof(tmp),r);
        printf("Sent Check Bits : %s\n", control);
        printf("Computed Check  : %s\n", tmp);
        equal = (strcasecmp(tmp, control) == 0);
    }
    else if (strcasecmp(method,"CRC16")==0) {
        uint16_t r = crc16_ccitt((const unsigned char*)data, strlen(data));
        char tmp[8]; sprintf(tmp,"%04X", r & 0xFFFF);
        printf("Sent Check Bits : %s\n", control);
        printf("Computed Check  : %s\n", tmp);
        equal = (strcasecmp(tmp, control) == 0);
    }
    else if (strcasecmp(method,"CRC32")==0) {
        uint32_t r = crc32_ieee((const unsigned char*)data, strlen(data));
        char tmp[16]; sprintf(tmp,"%08X", (unsigned int)r);
        printf("Sent Check Bits : %s\n", control);
        printf("Computed Check  : %s\n", tmp);
        equal = (strcasecmp(tmp, control) == 0);
    }
    else if (strcasecmp(method,"HAMMING")==0) {
        unsigned char enc[16384]; size_t enc_len=0; hamming74_encode_message((const unsigned char*)data, strlen(data), enc, &enc_len);
        char hexout[32768]; bytes_to_hex(enc,enc_len,hexout,sizeof(hexout));
        printf("Sent Check Bits : %s\n", control);
        printf("Computed Check  : %s\n", hexout);
        equal = (strcasecmp(hexout, control) == 0);
    }
    else if (strcasecmp(method,"CHECKSUM")==0 || strcasecmp(method,"INTERNET CHECKSUM")==0) {
        uint16_t cs = internet_checksum((const unsigned char*)data, strlen(data));
        char tmp[8]; sprintf(tmp,"%04X", cs);
        printf("Sent Check Bits : %s\n", control);
        printf("Computed Check  : %s\n", tmp);
        equal = (strcasecmp(tmp, control) == 0);
    } else {
        printf("Unknown method '%s'\n", method);
        equal = 0;
    }

    printf("Status : %s\n\n", (equal? "DATA CORRECT" : "DATA CORRUPTED"));
}

int main() {
    char server_ip[64];
    char portline[32];
    int server_port;

    printf("=== Client2 (Receiver + Checker) ===\n");
    printf("Server IP (default %s): ", SERVER_IP_DEFAULT);
    if (!fgets(server_ip,sizeof(server_ip),stdin)) return 0;
    if (server_ip[0] == '\n') strcpy(server_ip, SERVER_IP_DEFAULT);
    server_ip[strcspn(server_ip, "\n")] = 0;

    printf("Server port (default %d): ", SERVER_PORT_DEFAULT);
    if (!fgets(portline,sizeof(portline),stdin)) return 0;
    if (portline[0] == '\n') server_port = SERVER_PORT_DEFAULT;
    else server_port = atoi(portline);

    WSADATA wsa; if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { printf("WSA failed\n"); return 1; }

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv; serv.sin_family = AF_INET; serv.sin_port = htons(server_port); serv.sin_addr.s_addr = inet_addr(server_ip);

    printf("Connecting to server %s:%d ...\n", server_ip, server_port);
    if (connect(s,(struct sockaddr*)&serv,sizeof(serv)) < 0) { printf("Connect failed\n"); closesocket(s); return 1; }
    printf("Connected. Waiting for packets...\n");

    char buf[BUF_SIZE];
    while (1) {
        int r = recv(s, buf, sizeof(buf)-1, 0);
        if (r <= 0) { printf("Server disconnected or error\n"); break; }
        buf[r]=0;
        
        char *p1 = strchr(buf,'|');
        char *p2 = p1?strchr(p1+1,'|'):NULL;
        if (!p1 || !p2) {
            printf("Invalid packet from server: %s\n", buf);
            continue;
        }
        char data[BUF_SIZE]; char method[256]; char control[2048];
        size_t dlen = p1 - buf; if (dlen >= sizeof(data)) dlen = sizeof(data)-1;
        strncpy(data, buf, dlen); data[dlen]=0;
        size_t mlen = p2 - (p1+1); if (mlen >= sizeof(method)) mlen = sizeof(method)-1;
        strncpy(method, p1+1, mlen); method[mlen]=0;
        strncpy(control, p2+1, sizeof(control)-1); control[sizeof(control)-1]=0;

        printf("\n----- Received Packet -----\n");
        printf("Received Data : %s\n", data);
        printf("Method        : %s\n", method);
        recompute_and_compare(data, method, control);
    }

    closesocket(s); WSACleanup();
    return 0;
}
