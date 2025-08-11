#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define WIDTH 80
#define HEIGHT 30
#define OFFSETX 10
#define OFFSETY 5
#define DEFAULT_PORT 12345  // Fixed default port

typedef struct {
    int x, y;
    int dx, dy;
} Ball;

typedef struct {
    int x, y;
    int width;
} Paddle;

typedef struct {
    int paddle_x;
    int ball_x;
    int ball_y;
    int scoreA;
    int scoreB;
    int reset;
} ServerMessage;

typedef struct {
    int paddle_x;
} ClientMessage;

typedef struct {
    int x, y;
    clock_t timestamp;
} BallPosition;

// Global variables
Ball ball;
Paddle local_paddle;
Paddle opponent_paddle;
int scoreA = 0;
int scoreB = 0;
int game_running = 1;
int is_server = 0;
int sockfd;
struct sockaddr_in server_addr, client_addr;
pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
BallPosition prev_ball, curr_ball;
clock_t last_update_time = 0;

void init();
void end_game();
void draw(WINDOW *win);
void *move_ball(void *args);
void update_paddle(int ch);
void reset_ball();

int main(int argc, char *argv[]) {
    int port;

    // Check command-line arguments
    if (argc < 2) {
        fprintf(stderr, "Usage: %s server PORT or %s client <server_ip>\n", argv[0], argv[0]);
        exit(1);
    }

    if (strcmp(argv[1], "server") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s server PORT\n", argv[0]);
            exit(1);
        }
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number\n");
            exit(1);
        }
        is_server = 1;
    } else if (strcmp(argv[1], "client") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s client <server_ip>\n", argv[0]);
            exit(1);
        }
        port = DEFAULT_PORT;  // Use fixed port for client
        is_server = 0;
    } else {
        fprintf(stderr, "Invalid mode: use 'server' or 'client'\n");
        exit(1);
    }

    // Server setup
    if (is_server) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("socket creation failed");
            exit(1);
        }
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);
        if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("bind failed");
            exit(1);
        }
        if (listen(sockfd, 1) < 0) {
            perror("listen failed");
            exit(1);
        }
        printf("Server listening on port %d...\n", port);
        int client_len = sizeof(client_addr);
        int client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sockfd < 0) {
            perror("accept failed");
            exit(1);
        }
        close(sockfd);
        sockfd = client_sockfd;
        printf("Client connected.\n");
    } else {
        // Client setup
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("socket creation failed");
            exit(1);
        }
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, argv[2], &server_addr.sin_addr) <= 0) {
            perror("invalid server address");
            exit(1);
        }
        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("connection failed");
            exit(1);
        }
        printf("Connected to server at %s:%d\n", argv[2], port);
    }

    init();

    if (is_server) {
        local_paddle = (Paddle){WIDTH / 2 - 3, HEIGHT - 4, 10};
        opponent_paddle = (Paddle){WIDTH / 2 - 3, 1, 10};
        ball = (Ball){WIDTH / 2, HEIGHT / 2, 1, 1};
        pthread_t ball_thread;
        if (pthread_create(&ball_thread, NULL, move_ball, NULL) != 0) {
            perror("thread creation failed");
            exit(1);
        }
        pthread_detach(ball_thread);
    } else {
        local_paddle = (Paddle){WIDTH / 2 - 3, 1, 10};
        opponent_paddle = (Paddle){WIDTH / 2 - 3, HEIGHT - 4, 10};
    }

    while (game_running) {
        int ch = getch();
        if (ch == 'q') {
            game_running = 0;
            break;
        }
        update_paddle(ch);
        draw(stdscr);

        if (is_server) {
            ClientMessage client_msg;
            ssize_t bytes = recv(sockfd, &client_msg, sizeof(client_msg), 0);
            if (bytes <= 0) {
                printf("Client disconnected\n");
                game_running = 0;
                break;
            }
            opponent_paddle.x = client_msg.paddle_x;
        } else {
            ClientMessage client_msg = {local_paddle.x};
            if (send(sockfd, &client_msg, sizeof(client_msg), 0) < 0) {
                perror("send failed");
                game_running = 0;
                break;
            }
            ServerMessage server_msg;
            ssize_t bytes = recv(sockfd, &server_msg, sizeof(server_msg), 0);
            if (bytes <= 0) {
                printf("Server disconnected\n");
                game_running = 0;
                break;
            }
            opponent_paddle.x = server_msg.paddle_x;
            prev_ball = curr_ball;
            curr_ball.x = server_msg.ball_x;
            curr_ball.y = server_msg.ball_y;
            curr_ball.timestamp = clock();
            last_update_time = curr_ball.timestamp;
            scoreA = server_msg.scoreA;
            scoreB = server_msg.scoreB;
            if (server_msg.reset) {
                reset_ball();
            }
        }
    }

    close(sockfd);
    end_game();
    pthread_mutex_destroy(&game_mutex);
    return 0;
}

void init() {
    initscr();
    start_color();
    init_pair(1, COLOR_BLUE, COLOR_WHITE);
    init_pair(2, COLOR_YELLOW, COLOR_YELLOW);
    timeout(10);
    keypad(stdscr, TRUE);
    curs_set(FALSE);
    noecho();
    prev_ball.x = curr_ball.x = WIDTH / 2;
    prev_ball.y = curr_ball.y = HEIGHT / 2;
    prev_ball.timestamp = curr_ball.timestamp = 0;
}

void end_game() {
    endwin();
}

void draw(WINDOW *win) {
    clear();
    attron(COLOR_PAIR(1));
    for (int i = OFFSETX; i <= OFFSETX + WIDTH; i++) {
        mvprintw(OFFSETY - 1, i, " ");
    }
    mvprintw(OFFSETY - 1, OFFSETX + 3, "CS3205 PingPong, Ball: %d, %d", ball.x, ball.y);
    mvprintw(OFFSETY - 1, OFFSETX + WIDTH - 25, "Server: %d, Client: %d", scoreA, scoreB);
    for (int i = OFFSETY; i < OFFSETY + HEIGHT; i++) {
        mvprintw(i, OFFSETX, "  ");
        mvprintw(i, OFFSETX + WIDTH - 1, "  ");
    }
    for (int i = OFFSETX; i < OFFSETX + WIDTH; i++) {
        mvprintw(OFFSETY, i, " ");
        mvprintw(OFFSETY + HEIGHT - 1, i, " ");
    }
    attroff(COLOR_PAIR(1));

    if (is_server) {
        mvprintw(OFFSETY + ball.y, OFFSETX + ball.x, "o");
    } else {
        int time_since_update = last_update_time ? (int)((clock() - last_update_time) / (CLOCKS_PER_SEC / 1000)) : 0;
        float t = (float)time_since_update / 80.0f;
        if (t > 1.0f) t = 1.0f;
        int interp_x = prev_ball.x + (int)((curr_ball.x - prev_ball.x) * t);
        int interp_y = prev_ball.y + (int)((curr_ball.y - prev_ball.y) * t);
        mvprintw(OFFSETY + interp_y, OFFSETX + interp_x, "o");
    }

    attron(COLOR_PAIR(2));
    for (int i = 0; i < local_paddle.width; i++) {
        mvprintw(OFFSETY + local_paddle.y, OFFSETX + local_paddle.x + i, " ");
    }
    for (int i = 0; i < opponent_paddle.width; i++) {
        mvprintw(OFFSETY + opponent_paddle.y, OFFSETX + opponent_paddle.x + i, " ");
    }
    attroff(COLOR_PAIR(2));

    refresh();
}

void *move_ball(void *args) {
    while (game_running) {
        pthread_mutex_lock(&game_mutex);
        
        int next_x = ball.x + ball.dx;
        int next_y = ball.y + ball.dy;
        int should_reset = 0;

        if (next_x <= 2) {
            ball.dx = -ball.dx;
            next_x = 3;
        } else if (next_x >= WIDTH - 2) {
            ball.dx = -ball.dx;
            next_x = WIDTH - 3;
        }
        if (ball.dy < 0 && next_y <= opponent_paddle.y && ball.y > opponent_paddle.y &&
            next_x >= opponent_paddle.x - 1 && next_x <= opponent_paddle.x + opponent_paddle.width) {
            ball.dy = -ball.dy;
            next_y = opponent_paddle.y + 1;
        }
        if (ball.dy > 0 && next_y >= local_paddle.y && ball.y < local_paddle.y &&
            next_x >= local_paddle.x - 1 && next_x <= local_paddle.x + local_paddle.width) {
            ball.dy = -ball.dy;
            next_y = local_paddle.y - 1;
        }
        if (next_y <= 0) {
            scoreA++;
            should_reset = 1;
        } else if (next_y >= HEIGHT - 1) {
            scoreB++;
            should_reset = 1;
        }

        ball.x = next_x;
        ball.y = next_y;

        pthread_mutex_unlock(&game_mutex);

        ServerMessage server_msg = {local_paddle.x, ball.x, ball.y, scoreA, scoreB, should_reset};
        send(sockfd, &server_msg, sizeof(server_msg), 0);

        if (should_reset) {
            reset_ball();
        }

        usleep(80000);
    }
    return NULL;
}

void update_paddle(int ch) {
    pthread_mutex_lock(&game_mutex);
    
    if (ch == KEY_LEFT && local_paddle.x > 2) {
        local_paddle.x--;
    }
    if (ch == KEY_RIGHT && local_paddle.x < WIDTH - local_paddle.width - 2) {
        local_paddle.x++;
    }
    
    pthread_mutex_unlock(&game_mutex);
}

void reset_ball() {
    pthread_mutex_lock(&game_mutex);
    
    ball.x = WIDTH / 2;
    ball.y = HEIGHT / 2;
    ball.dx = 1;
    ball.dy = is_server ? 1 : -1;
    
    if (!is_server) {
        prev_ball.x = curr_ball.x = ball.x;
        prev_ball.y = curr_ball.y = ball.y;
        prev_ball.timestamp = curr_ball.timestamp = clock();
        last_update_time = curr_ball.timestamp;
    }
    
    pthread_mutex_unlock(&game_mutex);
}
