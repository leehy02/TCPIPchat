#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <signal.h>

#define SERV_IP "220.149.128.92"
#define SERV_PORT 4421
#define BACKLOG 10
#define MAX_CLIENTS 32

typedef struct {  
	int used;   //1이면 사용 중
	char user[20];
	char ip[32];
	int port;
	int hub_fd;       //허브 파일 디스크립터(sp[0])
} P2PEntry;

P2PEntry p2p_db[MAX_CLIENTS];  

static int recv_line(int fd, char *out, size_t out_sz) {
    size_t i = 0;
    while (i + 1 < out_sz) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) { out[i] = '\0'; return 0; }   // 연결 종료
        if (n < 0) return -1;                      // 에러
        if (c == '\n') break;                      // 한 줄 끝
        out[i++] = c;
    }
    out[i] = '\0';
    return (int)i;
}

static int send_line(int fd, const char *in, size_t in_sz){
    size_t i = 0;
    while(i < in_sz){
        char c = in[i];
        ssize_t n = send(fd, &c, 1, 0);
        if (n < 0) return -1;

        if (i == in_sz - 1){
            if (send(fd, "\n", 1, 0) < 0) return -1;
        }
        i++;
    }
    return (int)i;
}

#define INIT_MSG \
    "=====================================\n" \
    " Hello! I'm P2P File Sharing Server.\n" \
    " Please, LOG-IN!\n" \
    "=====================================\n"

int main(void)
{
    int hub_fds[MAX_CLIENTS];   
    int hub_cnt = 0;
	memset(p2p_db, 0, sizeof(p2p_db));

    fd_set rfds;

    int sockfd, new_fd;
    struct sockaddr_in my_addr, their_addr;
    unsigned int sin_size;

    int val = 1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1){
        perror("Server-socket() error lol!");
        exit(1);
    }
    else printf("Server-socket() sockfd is OK...\n");

    memset(&(my_addr.sin_zero), 0, 8);

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(SERV_PORT);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&val, sizeof(val))<0){
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    if(bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1)
    {
        perror("Server-bind() error lol!");
        exit(1);
    }
    else printf("Server-bind() is OK...\n");

    if(listen(sockfd, BACKLOG) == -1)
    {
        perror("listen() error lol!");
        exit(1);
    }
    else printf("listen() is OK...\n\n");

    char msg1[512] = "Log-in Success! - Welcome to GTalk";
    char msg2[512] = "----------Chatting Room----------";

    printf("\n%s\n", msg2);
	
	signal(SIGCHLD, SIG_IGN);  // 자식 종료 자동 수거

    while(1){
        sin_size = sizeof(their_addr);

        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);
        int cur_max = sockfd;

        for (int i = 0; i < hub_cnt; i++) {
            FD_SET(hub_fds[i], &rfds);
            if (hub_fds[i] > cur_max) cur_max = hub_fds[i];
        }

        int ready = select(cur_max + 1, &rfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            continue;
        }

        for (int i = 0; i < hub_cnt; i++) {
            int fd = hub_fds[i];
            if (FD_ISSET(fd, &rfds)) {   
                char line[700];
                int r = recv_line(fd, line, sizeof(line));

                if (r <= 0) {   //연결 종료시
					for (int k = 0; k < MAX_CLIENTS; k++)  //P2P_DB 전체 탐색
					{   
						if (p2p_db[k].used && p2p_db[k].hub_fd == fd) {  //연결종료된 fd 사용자 탐색
							printf("[DB] remove %s\n", p2p_db[k].user);   
							fflush(stdout);

							p2p_db[k].used = 0;   //해당 사용자 슬롯 비활성화
							break;
						}
					}
					
                    close(fd);
                    hub_fds[i] = hub_fds[hub_cnt - 1];
                    hub_cnt--;
                    i--;
                    continue;
                }
				
				if (strncmp(line, "[P2PREG]", 8) == 0)
				{
					char u[20], ip[32];
					int port;
	
					if (sscanf(line, "[P2PREG] %19s %31s %d", u, ip, &port) == 3)
					{
						int idx = -1;
						for (int k = 0; k < MAX_CLIENTS; k++)
						{
							if (p2p_db[k].used && strcmp(p2p_db[k].user, u) == 0)
							{
								idx = k;
								break;
							}
						}
						if (idx < 0)
						{
							for (int k = 0; k < MAX_CLIENTS; k++)
							{
								if (!p2p_db[k].used) { idx = k; break; }
							}
						}

						if (idx >= 0)
						{
							p2p_db[idx].used = 1;
							strncpy(p2p_db[idx].user, u, sizeof(p2p_db[idx].user)-1);
							strncpy(p2p_db[idx].ip, ip, sizeof(p2p_db[idx].ip)-1);
							p2p_db[idx].port = port;
							p2p_db[idx].hub_fd = fd;  //user의 허브 fd(=sp[0])
	
							printf("[DB] save %s %s %d (fd=%d)\n", p2p_db[idx].user, p2p_db[idx].ip, p2p_db[idx].port, fd);  //DB에 저장한 후 로그 출력
							fflush(stdout);
						}
					}
					continue;
				}				
				
				char *p = strstr(line, "[FILE]");
				if (p != NULL) {  //[FILE] <target>
					printf("[DEBUG] FILE cmd recv: %s\n", line);
					fflush(stdout);

					//sender(user1) 추출: line이 "[user1] ..."로 시작하니까 여기서 뽑음
					char sender[20] = {0};
					if (sscanf(line, "[%19[^]]]", sender) != 1) {
						send_line(fd, "[FILE] FAIL bad sender", strlen("[FILE] FAIL bad sender"));
						continue;
					}

					//target(user2) 추출
					char target[20] = {0};
					if (sscanf(p, "[FILE] %19s", target) != 1) {
						send_line(fd, "[FILE] USAGE: [FILE] <user_name>", strlen("[FILE] USAGE: [FILE] <user_name>"));
						continue;
					}

					//sender의 ip/port 찾기
					char s_ip[32] = {0};
					int  s_port = -1;
					int sender_ok = 0;

					for (int k = 0; k < MAX_CLIENTS; k++) {
						if (p2p_db[k].used && strcmp(p2p_db[k].user, sender) == 0) {
							strncpy(s_ip, p2p_db[k].ip, sizeof(s_ip)-1);
							s_port = p2p_db[k].port;
							sender_ok = 1;
							break;
						}
					}

					//target의 hub_fd 찾기
					int target_fd = -1;
					int target_ok = 0;

					for (int k = 0; k < MAX_CLIENTS; k++) {
						if (p2p_db[k].used && strcmp(p2p_db[k].user, target) == 0) {
							target_fd = p2p_db[k].hub_fd;
							target_ok = 1;
							break;
						}
					}

					if (!sender_ok) {
						send_line(fd, "[FILE] FAIL sender not registered", strlen("[FILE] FAIL sender not registered"));
						continue;
					}
					if (!target_ok) {
						send_line(fd, "[FILE] FAIL target not online", strlen("[FILE] FAIL target not online"));
						continue;
					}

					//target에게 sender의 ip/port 전달
					char out[128];
					snprintf(out, sizeof(out), "[FILE] %s %s %d", sender, s_ip, s_port);
					send_line(target_fd, out, strlen(out));

					continue;
				}
				
                printf("%s\n", line);

                for (int j = 0; j < hub_cnt; j++)
				{
                    if (hub_fds[j] == fd) continue;
                    send_line(hub_fds[j], line, strlen(line));
                }
                fflush(stdout);
            }
        }

        if (!FD_ISSET(sockfd, &rfds)) {
            continue;
        }

        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) {
            perror("socketpair");
            close(new_fd);
            continue;
        }

        pid_t pid = fork();

        if (pid == 0){
  
            close(sockfd);
            close(sp[0]);    

            send(new_fd, INIT_MSG, strlen(INIT_MSG) + 1, 0);

            char id[20];
            char pw[20];
            char msg[512];
            char buf[1024];

            recv(new_fd, id, sizeof(id), 0);
            recv(new_fd, pw, sizeof(pw), 0);

            if ((strcmp(id, "user1") == 0 && strcmp(pw, "passwd1") == 0) ||
                (strcmp(id, "user2") == 0 && strcmp(pw, "passwd2") == 0) ||
                (strcmp(id, "user3") == 0 && strcmp(pw, "passwd3") == 0)){

                send_line(new_fd, "success", strlen("success"));
                send_line(new_fd, msg1, strlen(msg1));
                send_line(new_fd, msg2, strlen(msg2));
				
				char regline[128];
				int p2p_port = -1;

				int wid = recv_line(new_fd, regline, sizeof(regline)); //받은 정보 길이
				if (wid <= 0)
				{
					printf("[P2PREG] recv_line failed\n"); //실패
					fflush(stdout);
				}
				else
				{
					if (sscanf(regline, "[P2P_IP] %d", &p2p_port) == 1)   //받은 포트 형식 확인
					{
						char port_msg[128];
						snprintf(port_msg, sizeof(port_msg), "[P2PREG] %s %s %d", id, inet_ntoa(their_addr.sin_addr), p2p_port);
						send_line(sp[1], port_msg, strlen(port_msg));   //부모에게 ip/port 저장
						
						printf("[P2PREG] user=%s ip=%s port=%d\n", id, inet_ntoa(their_addr.sin_addr), p2p_port);
						fflush(stdout);
					}
					else
					{
						printf("[P2PREG] invalid format: %s\n", regline);
						fflush(stdout);
					}
				}

                fd_set crfds;
                int m = (new_fd > sp[1]) ? new_fd : sp[1];

                while(1){
                    FD_ZERO(&crfds);
                    FD_SET(new_fd, &crfds);
                    FD_SET(sp[1], &crfds);

                    int rdy = select(m + 1, &crfds, NULL, NULL, NULL);
                    if (rdy < 0) {
                        if (errno == EINTR) continue;
                        break;
                    }

                    if (FD_ISSET(new_fd, &crfds)) {
                        int r = recv_line(new_fd, msg, sizeof(msg));
                        if (r <= 0) break;
                        if (strcmp(msg, "exit") == 0) break;

                        snprintf(buf, sizeof(buf), "[%s] %s", id, msg);
                        send_line(sp[1], buf, strlen(buf));
                    }

                    if (FD_ISSET(sp[1], &crfds)) {
                        char line[700];
                        int r = recv_line(sp[1], line, sizeof(line));
                        if (r <= 0) break;

                        send_line(new_fd, line, strlen(line));
                    }
                }

            } else {
                strcpy(msg, "Log-in fail: Incorrect password...");
                send_line(new_fd, "fail", strlen("fail"));
                send_line(new_fd, msg, strlen(msg));
            }

            close(sp[1]);
            close(new_fd);
            _exit(0);
        }
        else if (pid > 0) {

            close(new_fd);
            close(sp[1]);  

            if (hub_cnt < MAX_CLIENTS) {
                hub_fds[hub_cnt++] = sp[0];
            } else {
                close(sp[0]); 
            }
        }
        else {
            perror("fork");
            close(new_fd);
            close(sp[0]);
            close(sp[1]);
        }
    }

    close(sockfd);
    return 0;
}
