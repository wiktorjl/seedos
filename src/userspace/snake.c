/*
 * snake.c - Classic Snake Game for SeedOS
 *
 * Use arrow keys to navigate the snake and eat food (*).
 * The snake grows each time it eats.
 * Game ends if you hit a wall or yourself.
 *
 * Controls:
 *   Arrow keys - Move snake
 *   q          - Quit game
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Game board dimensions */
#define BOARD_WIDTH  40
#define BOARD_HEIGHT 15

/* Maximum snake length */
#define MAX_SNAKE_LEN 256

/* Direction constants */
#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

/* Game state */
static int snake_x[MAX_SNAKE_LEN];
static int snake_y[MAX_SNAKE_LEN];
static int snake_len;
static int direction;
static int food_x, food_y;
static int score;
static int game_over;

/* Simple pseudo-random number generator */
static unsigned int rand_state = 12345;

static int rand_int(int max) {
    rand_state = rand_state * 1103515245 + 12345;
    return (rand_state >> 16) % max;
}

/* Seed the RNG with uptime */
static void rand_seed(void) {
    rand_state = (unsigned int)uptime();
}

/* Clear screen using ANSI escape codes */
static void clear_screen(void) {
    printf("\033[2J\033[H");
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

    int start_x = BOARD_WIDTH / 2;
    int start_y = BOARD_HEIGHT / 2;

    for(int i = 0; i < snake_len; i++) {
        snake_x[i] = start_x - i;
        snake_y[i] = start_y;
    }

    score = 0;
    game_over = 0;

    place_food();
}

/* Draw the game board */
static void draw_board(void) {
    clear_screen();

    /* Create board buffer */
    char board[BOARD_HEIGHT][BOARD_WIDTH + 1];

    /* Fill with spaces */
    for(int y = 0; y < BOARD_HEIGHT; y++) {
        for(int x = 0; x < BOARD_WIDTH; x++) {
            board[y][x] = ' ';
        }
        board[y][BOARD_WIDTH] = '\0';
    }

    /* Draw walls */
    for(int x = 0; x < BOARD_WIDTH; x++) {
        board[0][x] = '#';
        board[BOARD_HEIGHT - 1][x] = '#';
    }
    for(int y = 0; y < BOARD_HEIGHT; y++) {
        board[y][0] = '#';
        board[y][BOARD_WIDTH - 1] = '#';
    }

    /* Draw food */
    board[food_y][food_x] = '*';

    /* Draw snake */
    for(int i = 0; i < snake_len; i++) {
        if(i == 0) {
            board[snake_y[i]][snake_x[i]] = 'O';  /* Head */
        } else {
            board[snake_y[i]][snake_x[i]] = 'o';  /* Body */
        }
    }

    /* Print board */
    for(int y = 0; y < BOARD_HEIGHT; y++) {
        printf("%s\n", board[y]);
    }

    /* Print score */
    printf("\nScore: %d  |  Length: %d  |  Press arrow keys to move, 'q' to quit\n",
           score, snake_len);
}

/* Move the snake */
static void move_snake(void) {
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
            /* Shift and add new segment */
            for(int i = snake_len; i > 0; i--) {
                snake_x[i] = snake_x[i - 1];
                snake_y[i] = snake_y[i - 1];
            }
            snake_len++;
            score += 10;
            place_food();
        }
    }

    /* Set new head position */
    snake_x[0] = new_x;
    snake_y[0] = new_y;
}

/* Read a keypress (handles escape sequences for arrows) */
static int read_key(void) {
    int c = getchar();

    if(c == 27) {  /* ESC - start of escape sequence */
        int c2 = getchar();
        if(c2 == '[') {
            int c3 = getchar();
            switch(c3) {
                case 'A': return 'U';  /* Up */
                case 'B': return 'D';  /* Down */
                case 'C': return 'R';  /* Right */
                case 'D': return 'L';  /* Left */
            }
        }
        return 0;  /* Unknown escape sequence */
    }

    return c;
}

/* Main game loop */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("=== SNAKE ===\n\n");
    printf("Eat the food (*) to grow!\n");
    printf("Don't hit the walls or yourself!\n\n");
    printf("Controls: Arrow keys to move, 'q' to quit\n\n");
    printf("Press any key to start...\n");

    getchar();  /* Wait for key */

    init_game();

    while(!game_over) {
        draw_board();

        int key = read_key();

        /* Handle quit */
        if(key == 'q' || key == 'Q') {
            break;
        }

        /* Handle direction change (prevent 180-degree turns) */
        switch(key) {
            case 'U':  /* Up */
                if(direction != DIR_DOWN) direction = DIR_UP;
                break;
            case 'D':  /* Down */
                if(direction != DIR_UP) direction = DIR_DOWN;
                break;
            case 'L':  /* Left */
                if(direction != DIR_RIGHT) direction = DIR_LEFT;
                break;
            case 'R':  /* Right */
                if(direction != DIR_LEFT) direction = DIR_RIGHT;
                break;
        }

        move_snake();
    }

    /* Game over screen */
    draw_board();

    if(game_over) {
        printf("\n*** GAME OVER ***\n");
    }
    printf("Final Score: %d\n", score);
    printf("Snake Length: %d\n", snake_len);

    printf("\nPress any key to exit...\n");
    getchar();

    return 0;
}
