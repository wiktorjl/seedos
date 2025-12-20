/*
 * snake.c - Classic Snake Game for SeedOS
 *
 * A proper snake game with automatic movement!
 * The snake moves on its own - you just steer it.
 *
 * Controls:
 *   Arrow keys - Change direction
 *   q          - Quit game
 *   p          - Pause/unpause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Game board dimensions */
#define BOARD_WIDTH  40
#define BOARD_HEIGHT 18

/* Maximum snake length */
#define MAX_SNAKE_LEN 256

/* Game speed (milliseconds per frame) */
#define FRAME_TIME_MS 150

/* Direction constants */
#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RED     "\033[31m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_BOLD    "\033[1m"

/* Game state */
static int snake_x[MAX_SNAKE_LEN];
static int snake_y[MAX_SNAKE_LEN];
static int snake_len;
static int direction;
static int next_direction;  /* Buffered direction change */
static int food_x, food_y;
static int score;
static int game_over;
static int paused;
static int high_score = 0;

/* Simple pseudo-random number generator */
static unsigned int rand_state = 12345;

static int rand_int(int max) {
    rand_state = rand_state * 1103515245 + 12345;
    return (rand_state >> 16) % max;
}

static void rand_seed(void) {
    rand_state = (unsigned int)uptime();
}

/* Clear screen and move cursor home */
static void clear_screen(void) {
    printf("\033[2J\033[H");
}

/* Hide cursor */
static void hide_cursor(void) {
    printf("\033[?25l");
}

/* Show cursor */
static void show_cursor(void) {
    printf("\033[?25h");
}

/* Place food at random empty location */
static void place_food(void) {
    int attempts = 0;
    while(attempts < 100) {
        food_x = 1 + rand_int(BOARD_WIDTH - 2);
        food_y = 1 + rand_int(BOARD_HEIGHT - 2);

        /* Check it's not on snake */
        int on_snake = 0;
        for(int i = 0; i < snake_len; i++) {
            if(snake_x[i] == food_x && snake_y[i] == food_y) {
                on_snake = 1;
                break;
            }
        }
        if(!on_snake) break;
        attempts++;
    }
}

/* Initialize game state */
static void init_game(void) {
    rand_seed();

    /* Start snake in center, length 3, going right */
    snake_len = 3;
    direction = DIR_RIGHT;
    next_direction = DIR_RIGHT;

    int start_x = BOARD_WIDTH / 2;
    int start_y = BOARD_HEIGHT / 2;

    for(int i = 0; i < snake_len; i++) {
        snake_x[i] = start_x - i;
        snake_y[i] = start_y;
    }

    score = 0;
    game_over = 0;
    paused = 0;

    place_food();
}

/* Draw the game board */
static void draw_board(void) {
    clear_screen();
    hide_cursor();

    /* Title */
    printf("%s%s", COLOR_BOLD, COLOR_CYAN);
    printf("                    SNAKE\n");
    printf("%s", COLOR_RESET);

    /* Create board buffer */
    char board[BOARD_HEIGHT][BOARD_WIDTH + 1];
    char colors[BOARD_HEIGHT][BOARD_WIDTH];

    /* Fill with spaces */
    for(int y = 0; y < BOARD_HEIGHT; y++) {
        for(int x = 0; x < BOARD_WIDTH; x++) {
            board[y][x] = ' ';
            colors[y][x] = 'W';  /* White/default */
        }
        board[y][BOARD_WIDTH] = '\0';
    }

    /* Draw walls with box-drawing style */
    for(int x = 0; x < BOARD_WIDTH; x++) {
        board[0][x] = '#';
        board[BOARD_HEIGHT - 1][x] = '#';
        colors[0][x] = 'C';  /* Cyan */
        colors[BOARD_HEIGHT - 1][x] = 'C';
    }
    for(int y = 0; y < BOARD_HEIGHT; y++) {
        board[y][0] = '#';
        board[y][BOARD_WIDTH - 1] = '#';
        colors[y][0] = 'C';
        colors[y][BOARD_WIDTH - 1] = 'C';
    }

    /* Draw food */
    board[food_y][food_x] = '*';
    colors[food_y][food_x] = 'R';  /* Red */

    /* Draw snake */
    for(int i = 0; i < snake_len; i++) {
        int x = snake_x[i];
        int y = snake_y[i];
        if(x >= 0 && x < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT) {
            if(i == 0) {
                /* Head - show direction */
                switch(direction) {
                    case DIR_UP:    board[y][x] = '^'; break;
                    case DIR_DOWN:  board[y][x] = 'v'; break;
                    case DIR_LEFT:  board[y][x] = '<'; break;
                    case DIR_RIGHT: board[y][x] = '>'; break;
                }
                colors[y][x] = 'G';  /* Green */
            } else {
                board[y][x] = 'o';
                colors[y][x] = 'Y';  /* Yellow for body */
            }
        }
    }

    /* Print board with colors */
    for(int y = 0; y < BOARD_HEIGHT; y++) {
        for(int x = 0; x < BOARD_WIDTH; x++) {
            switch(colors[y][x]) {
                case 'G': printf("%s", COLOR_GREEN); break;
                case 'Y': printf("%s", COLOR_YELLOW); break;
                case 'R': printf("%s%s", COLOR_BOLD, COLOR_RED); break;
                case 'C': printf("%s", COLOR_CYAN); break;
                default: printf("%s", COLOR_WHITE); break;
            }
            putchar(board[y][x]);
        }
        printf("%s\n", COLOR_RESET);
    }

    /* Print score bar */
    printf("%s", COLOR_BOLD);
    printf(" Score: %s%d%s", COLOR_GREEN, score, COLOR_RESET);
    printf("%s  |  Length: %s%d%s", COLOR_BOLD, COLOR_YELLOW, snake_len, COLOR_RESET);
    printf("%s  |  High: %s%d%s", COLOR_BOLD, COLOR_CYAN, high_score, COLOR_RESET);

    if(paused) {
        printf("  %s[PAUSED]%s", COLOR_RED, COLOR_RESET);
    }
    printf("\n");

    printf(" %sArrows%s=move  %sp%s=pause  %sq%s=quit\n",
           COLOR_CYAN, COLOR_RESET,
           COLOR_CYAN, COLOR_RESET,
           COLOR_CYAN, COLOR_RESET);
}

/* Move the snake */
static void move_snake(void) {
    /* Apply buffered direction change */
    direction = next_direction;

    /* Calculate new head position */
    int new_x = snake_x[0];
    int new_y = snake_y[0];

    switch(direction) {
        case DIR_UP:    new_y--; break;
        case DIR_DOWN:  new_y++; break;
        case DIR_LEFT:  new_x--; break;
        case DIR_RIGHT: new_x++; break;
    }

    /* Check wall collision */
    if(new_x <= 0 || new_x >= BOARD_WIDTH - 1 ||
       new_y <= 0 || new_y >= BOARD_HEIGHT - 1) {
        game_over = 1;
        return;
    }

    /* Check self collision (excluding tail since it will move) */
    for(int i = 0; i < snake_len - 1; i++) {
        if(snake_x[i] == new_x && snake_y[i] == new_y) {
            game_over = 1;
            return;
        }
    }

    /* Check food collision */
    int ate_food = (new_x == food_x && new_y == food_y);

    /* Move body (shift all segments) */
    if(!ate_food) {
        /* Normal move - shift body, tail disappears */
        for(int i = snake_len - 1; i > 0; i--) {
            snake_x[i] = snake_x[i - 1];
            snake_y[i] = snake_y[i - 1];
        }
    } else {
        /* Ate food - grow snake */
        if(snake_len < MAX_SNAKE_LEN) {
            for(int i = snake_len; i > 0; i--) {
                snake_x[i] = snake_x[i - 1];
                snake_y[i] = snake_y[i - 1];
            }
            snake_len++;
            score += 10;
            if(score > high_score) {
                high_score = score;
            }
            place_food();
        }
    }

    /* Set new head position */
    snake_x[0] = new_x;
    snake_y[0] = new_y;
}

/* Process available input (non-blocking) */
static int process_input(void) {
    while(kbhit()) {
        int c = getchar();

        if(c == 'q' || c == 'Q') {
            return -1;  /* Quit */
        }

        if(c == 'p' || c == 'P') {
            paused = !paused;
            continue;
        }

        /* Handle escape sequences (arrow keys) */
        if(c == 27) {
            if(kbhit()) {
                int c2 = getchar();
                if(c2 == '[' && kbhit()) {
                    int c3 = getchar();
                    switch(c3) {
                        case 'A':  /* Up */
                            if(direction != DIR_DOWN)
                                next_direction = DIR_UP;
                            break;
                        case 'B':  /* Down */
                            if(direction != DIR_UP)
                                next_direction = DIR_DOWN;
                            break;
                        case 'C':  /* Right */
                            if(direction != DIR_LEFT)
                                next_direction = DIR_RIGHT;
                            break;
                        case 'D':  /* Left */
                            if(direction != DIR_RIGHT)
                                next_direction = DIR_LEFT;
                            break;
                    }
                }
            }
        }
    }
    return 0;
}

/* Wait until next frame */
static void wait_frame(unsigned long *last_time) {
    unsigned long now;
    do {
        now = uptime();
        /* Small yield to not busy-wait too hard */
    } while(now - *last_time < FRAME_TIME_MS);
    *last_time = now;
}

/* Main game loop */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    clear_screen();
    hide_cursor();

    printf("%s%s", COLOR_BOLD, COLOR_GREEN);
    printf("\n");
    printf("        ####  #   #   ##   #  #  ##### \n");
    printf("       #      ##  #  #  #  # #   #     \n");
    printf("        ###   # # #  ####  ##    ####  \n");
    printf("           #  #  ##  #  #  # #   #     \n");
    printf("       ####   #   #  #  #  #  #  ##### \n");
    printf("%s\n", COLOR_RESET);
    printf("\n");
    printf("  %sEat the food %s*%s to grow!%s\n",
           COLOR_WHITE, COLOR_RED, COLOR_WHITE, COLOR_RESET);
    printf("  %sDon't hit the walls or yourself!%s\n\n",
           COLOR_WHITE, COLOR_RESET);
    printf("  %sControls:%s\n", COLOR_CYAN, COLOR_RESET);
    printf("    Arrow keys - Change direction\n");
    printf("    P          - Pause\n");
    printf("    Q          - Quit\n");
    printf("\n");
    printf("  %sPress any key to start...%s\n", COLOR_YELLOW, COLOR_RESET);

    show_cursor();
    getchar();

    init_game();

    unsigned long last_frame = uptime();

    while(!game_over) {
        draw_board();

        /* Process any pending input */
        if(process_input() < 0) {
            break;  /* User quit */
        }

        /* Wait for next frame */
        wait_frame(&last_frame);

        /* Move snake if not paused */
        if(!paused) {
            move_snake();
        }
    }

    /* Game over screen */
    draw_board();
    show_cursor();

    printf("\n");
    if(game_over) {
        printf("  %s%s*** GAME OVER ***%s\n", COLOR_BOLD, COLOR_RED, COLOR_RESET);
    }
    printf("  %sFinal Score: %s%d%s\n", COLOR_WHITE, COLOR_GREEN, score, COLOR_RESET);
    printf("  %sSnake Length: %s%d%s\n", COLOR_WHITE, COLOR_YELLOW, snake_len, COLOR_RESET);

    if(score >= high_score && score > 0) {
        printf("  %s%sNEW HIGH SCORE!%s\n", COLOR_BOLD, COLOR_CYAN, COLOR_RESET);
    }

    printf("\n  Press any key to exit...\n");
    getchar();

    clear_screen();
    show_cursor();

    return 0;
}
