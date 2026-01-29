#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/wait.h>

#define SERV_IP "220.149.128.92"
#define SERV_PORT 4421
#define P2P_PORT 4422
#define MAX_FILES 100

static char file_list[MAX_FILES][256];
static int file_count = 0;

static void trimnl(char *s){
    size_t n = strlen(s);
    if (n && s[n-1] == '\n') s[n-1] = '\0';
}

static int recv_line(int fd, char *out, size_t out_sz) {
    size_t i = 0;
    while (i + 1 < out_sz) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) { out[i] = '\0'; return 0; }
        if (n < 0) return -1;
        if (c == '\n') break;
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

static void start_file(const char *ip, int port)   //파일 보내기
{
	int s;
	struct sockaddr_in addr;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) { perror("p2p socket"); return; }

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
		printf("invalid ip: %s\n", ip);
		close(s);
		return;
	}

	if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("p2p connect");
		close(s);
		return;
	}

	printf("P2P connected to %s:%d\n", ip, port);

	DIR *dp = opendir(".");          //현재 디렉터리
	struct dirent *ent;

	send_line(s, "[LIST]", strlen("[LIST]"));

	if (dp != NULL) {
		while ((ent = readdir(dp)) != NULL) { 
			if (strstr(ent->d_name, ".txt") != NULL) {   //.txt 파일만 전송
				send_line(s, ent->d_name, strlen(ent->d_name));
			}
		}
		closedir(dp);
	}

	send_line(s, "[END]", strlen("[END]"));
	
	//상대의 [GET] 기다리기
	char cmd[300];
	int r = recv_line(s, cmd, sizeof(cmd));
	if (r > 0 && strncmp(cmd, "[GET] ", 6) == 0) {
		char fname[256];
		
		if (sscanf(cmd, "[GET] %255s", fname) == 1) {  //파일명 추출
			printf("[P2P] GET request: %s\n", fname);

			FILE *fp = fopen(fname, "r");
			if (!fp) {
				send_line(s, "[ERR] nofile", strlen("[ERR] nofile"));
				} else {
				send_line(s, "[DATA]", strlen("[DATA]"));

				char line[512];
				while (fgets(line, sizeof(line), fp)) {
					trimnl(line);
					send_line(s, line, strlen(line));
				}

				fclose(fp);
				send_line(s, "[DONE]", strlen("[DONE]"));
			}
		}
	}

	close(s);

	
}

static void p2p_listen(int port)  //파일 받기
{
	int listen_s = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_s < 0) { perror("p2p socket"); return; }

	int yes = 1;
	setsockopt(listen_s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(listen_s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("p2p bind");
		close(listen_s);
		return;
	}

	if (listen(listen_s, 5) < 0) {
		perror("p2p listen");
		close(listen_s);
		return;
	}

	printf("[P2P] listening on %d\n", port);
	fflush(stdout);

	while (1) {
		struct sockaddr_in cli;
		socklen_t cli_len = sizeof(cli);
		int client_s = accept(listen_s, (struct sockaddr*)&cli, &cli_len);
		if (client_s < 0) { perror("p2p accept"); continue; }

		printf("[P2P] accepted from %s:%d\n",
		inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));
		fflush(stdout);

		char line[256];

		//첫 줄 받기 [LIST]
		int r = recv_line(client_s, line, sizeof(line));
		if (r <= 0) {
			close(client_s);
			continue;
		}

		if (strcmp(line, "[LIST]") == 0) {

			//받아오기 전 초기화
			file_count = 0;
			printf("----- take file list -----\n");
			fflush(stdout);

			//[END]까지 수신하면서 저장
			while (1) {
				r = recv_line(client_s, line, sizeof(line));
				if (r <= 0) break;
				if (strcmp(line, "[END]") == 0) break;

				if (file_count < MAX_FILES) {
					strncpy(file_list[file_count], line,
					sizeof(file_list[file_count]) - 1);
					file_list[file_count][sizeof(file_list[file_count]) - 1] = '\0';
					file_count++;
				}
			}

			//번호 붙여 출력
			for (int i = 0; i < file_count; i++) {
				printf("%d) %s\n", i + 1, file_list[i]);
			}
			printf("--------- End list --------\n");
			fflush(stdout);

			//사용자 선택 입력 받기
			if (file_count > 0) {
				int sel = 0;
				printf("Select file number (1~%d, 0=cancel): ", file_count);
				fflush(stdout);

				if (scanf("%d", &sel) == 1) {
					//scanf 뒤에 남은 개행 제거
					int ch;
					while ((ch = getchar()) != '\n' && ch != EOF) {}

					if (sel == 0) {
						printf("[INFO] canceled.\n");
					}
					else if (sel >= 1 && sel <= file_count) {
						printf("[INFO] selected: %s\n", file_list[sel - 1]);

						char req[300];
						snprintf(req, sizeof(req),
						"[GET] %s", file_list[sel - 1]);  //파일번호 선택
						send_line(client_s, req, strlen(req));

						char recv_buffer[512];
						int recv_len = recv_line(client_s,
						recv_buffer,
						sizeof(recv_buffer));

						if (recv_len > 0 && strcmp(recv_buffer, "[DATA]") == 0) {
							char outname[300];
							snprintf(outname, sizeof(outname),
							"recv_%s", file_list[sel - 1]);

							FILE *out = fopen(outname, "w");
							if (!out) {
								printf("[ERR] cannot create %s\n", outname);
							}
							else {
								while (1) {
									recv_len = recv_line(client_s,
									recv_buffer,
									sizeof(recv_buffer));
									if (recv_len <= 0) break;
									if (strcmp(recv_buffer, "[DONE]") == 0) break;

									fprintf(out, "%s\n", recv_buffer);
								}
								fclose(out);
								printf("[INFO] saved: %s\n", outname);
							}
						}
						else if (recv_len > 0 &&
						strncmp(recv_buffer, "[ERR]", 5) == 0) {
							printf("%s\n", recv_buffer);
						}
						else {
							printf("[WARN] unexpected response: %s\n", recv_buffer);
						}
						fflush(stdout);
					}
					else {
						printf("[WARN] invalid selection.\n");
					}
				}
				else {
					//입력이 숫자가 아니면 정리
					int ch;
					while ((ch = getchar()) != '\n' && ch != EOF) {}
					printf("[WARN] invalid input.\n");
				}
				fflush(stdout);
			}
			else {
				printf("[INFO] no .txt files received.\n");
				fflush(stdout);
			}
		}
		else {
			printf("[P2P-RECV] %s\n", line);
			fflush(stdout);
		}
		close(client_s);
	}
}

int main()
{
    int server_socket;
    struct sockaddr_in dest_addr;
    char buf[512];

    char id[20];
    char pw[20];
    char msg[512];
    char status[20];

    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if(server_socket == -1)
    {
        perror("Client-socket() error lol!");
        exit(1);
    }else printf("Client-socket() server_socket is OK...\n");

    memset(&(dest_addr.sin_zero), 0, 8);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERV_PORT);
    dest_addr.sin_addr.s_addr = inet_addr(SERV_IP);

    if(connect(server_socket, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr)) == -1)
    {
        perror("Client-connect() error lol");
        exit(1);
    }else printf("Client-conncect() is OK...\n\n");

    recv(server_socket, buf, sizeof(buf), 0);
    printf("%s", buf);

    printf("ID: ");
    fflush(stdout);
    fgets(id, sizeof(id), stdin);
    trimnl(id);

    printf("PW: ");
    fflush(stdout);
    fgets(pw, sizeof(pw), stdin);
    trimnl(pw);

    send(server_socket, id, sizeof(id), 0);
    send(server_socket, pw, sizeof(pw), 0);

    // ----chatting room----
    recv_line(server_socket, status, sizeof(status));

    if(strcmp(status,"success")==0){
		
		char p2p_ip_port[64];
		snprintf(p2p_ip_port, sizeof(p2p_ip_port), "[P2P_IP] %d", P2P_PORT); //포트정보 문자열 생성
		send_line(server_socket, p2p_ip_port, strlen(p2p_ip_port));
		

		pid_t pid = fork();
		
		if (pid == 0) {   //p2p 수신용
			p2p_listen(P2P_PORT);
			_exit(0);
		}
		
        printf("\n");
        recv_line(server_socket, msg, sizeof(msg));
        printf("%s\n", msg);

        recv_line(server_socket, msg, sizeof(msg));
        printf("%s\n", msg);

        // ? 첫 메시지도 >> 로 시작
        printf(">>");
        fflush(stdout);

        // ? 채팅 중: (키보드 입력 + 서버 메시지) 동시 처리
        while(1){
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(server_socket, &rfds);
            FD_SET(STDIN_FILENO, &rfds);

            int maxfd = (server_socket > STDIN_FILENO) ? server_socket : STDIN_FILENO;
            int rdy = select(maxfd + 1, &rfds, NULL, NULL, NULL);
            if (rdy < 0) break;
			
            // (1) 서버에서 메시지 옴: 현재 줄 지우고 출력 후 >> 복구
            if (FD_ISSET(server_socket, &rfds)) {
                int r = recv_line(server_socket, msg, sizeof(msg));
                if (r <= 0) break;
				
				if (strncmp(msg, "[FILE]", 6) == 0) {
					char p2p_user[20], p2p_ip[32];
					int p2p_port;

					if (sscanf(msg, "[FILE] %19s %31s %d", p2p_user, p2p_ip, &p2p_port) == 3) {
						printf("\r\033[2K");
						printf("%s\n", msg);   //메시지 그대로 출력
						fflush(stdout);

						start_file(p2p_ip, p2p_port); //요청자에게 연결 시도

						printf(">>");
						fflush(stdout);
						continue;
					}
				}
				
				if (strncmp(msg, "[P2PINFO]", 9) == 0) {  //IP 받았을때
					char target[20], ip[32];
					int port;
					if (sscanf(msg, "[P2PINFO] %19s %31s %d", target, ip, &port) == 3) {
						printf("\r\033[2K");
						printf("%s\n", msg);
						start_file(ip, port);   //P2P 연결 시작
						printf(">>");
						fflush(stdout);
						continue;
					}
				}	
				

                // ? ">> 옆으로" 보이게: 현재 입력줄 제거 → 메시지 출력 → 프롬프트 복원
                printf("\r\033[2K");     // 커서 맨앞 + 현재 줄 삭제
                printf("%s\n", msg);     // 상대 메시지 출력
                printf(">>");            // 프롬프트 복구
                fflush(stdout);
            }

            //내가 입력함: send_line으로 서버에 전송
            if (FD_ISSET(STDIN_FILENO, &rfds)) {
                if (!fgets(msg, sizeof(msg), stdin)) break;
                trimnl(msg);

                if (strcmp(msg, "exit")==0) {
                    send_line(server_socket, msg, strlen(msg));
                    break;
                }

                send_line(server_socket, msg, strlen(msg));
				
				

                // 입력 후 다음 프롬프트 다시
                printf(">>");
                fflush(stdout);
            }

        }
	if (pid > 0) {  //생성된 자식 프로세스 종료
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
	}

    } else if(strcmp(status,"fail")==0) {
        printf("\n");
        recv_line(server_socket, msg, sizeof(msg));
        printf("%s\n", msg);
    }

    close(server_socket);
    return 0;
}
