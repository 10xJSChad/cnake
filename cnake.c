#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>


#define GAMEGRID_ROWS    15
#define GAMEGRID_COLUMNS 30
#define ERROR_EXIT(err_string)          \
    do {                                \
        system("clear");                \
        printf(err_string "\n");        \
        exit(1);                        \
    } while(0)
#define SnakeGame_MACRO_PrintAndTerminate(snakegame, ...)           \
    do {                                                            \
        system("clear");                                            \
        printf(__VA_ARGS__);                                        \
        SnakeGame_Destroy(snakegame);                               \
        exit(1);                                                    \
    } while (0)
#define ERROR_EXIT_IF_NULL(ptr, err_string) if (ptr == NULL) ERROR_EXIT(err_string)
#define POS_TO_INDEX(columns, column, row)  (columns * row + column)
#define SLEEP_MS(ms)                        usleep(1000 * ms)
#define RANDINT(min, max)                   ((rand() % (max - min + 1)) + min);
#define GAME_TICK_INTERVAL 100


enum SnakeConstants {
    SNAKE_OBJTYPE = 1,
    SNAKE_SPRITE  = 'S',
    SNAKE_DIR_UP,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
    SNAKE_DIR_RIGHT
};


enum FoodConstants {
    FOOD_OBJTYPE = 2,
    FOOD_SPRITE  = 'F'
};


enum MiscConstants {
    NONE_OBJTYPE = 0,
    NONE_SPRITE  = '.'
};


struct GameObject {
    char  x;
    char  y;
    char  type;
    char  sprite;
};


struct Snake {
    struct GameObject** parts;
    struct GameObject*  head;
    struct GameObject*  tail;
    int  length;
    char direction;
};


struct Screen {
    int   rows;
    int   columns;
    char* draw_buffer;
    char* print_buffer;
};


struct SnakeGame {
    struct Snake*        snake;
    struct GameObject*   food;
    struct Screen*       screen;
    struct InputHandler* inputhandler;
    char*                grid;
};


struct InputHandler {
    struct termios oldt;
    struct termios newt;
    char input;
};


void
InputHandler_Destroy(struct InputHandler* inputhandler)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &inputhandler->oldt);
    free(inputhandler);
}


struct InputHandler*
InputHandler_Create(void)
{
    struct InputHandler* inputhandler = malloc(sizeof *inputhandler);

    tcgetattr(STDIN_FILENO, &inputhandler->oldt);

    inputhandler->input = '\0';
    inputhandler->newt  = inputhandler->oldt;
    inputhandler->newt.c_lflag &= ~(ICANON);

    tcsetattr(STDIN_FILENO, TCSANOW, &inputhandler->newt);

    return inputhandler;
}


int
InputHandler_KeyPressed(void)
{
    fd_set fds;
    struct timeval tv = { 0L, 0L };

    FD_ZERO(&fds);
    FD_SET(0, &fds);

    return select(1, &fds, NULL, NULL, &tv) > 0;
}


unsigned char
InputHandler_GetChar(void)
{
    int r;
    unsigned char ch;

    if ((r = read(0, &ch, sizeof(ch))) < 0) {
        ERROR_EXIT("InputHandler_GetChar(): Invalid Character");
    } else {
        return ch;
    }
}


void
Screen_Destroy(struct Screen* screen)
{
    free(screen->draw_buffer);
    free(screen->print_buffer);
    free(screen);
}


struct Screen*
Screen_Create(int rows,
              int columns)
{
    struct Screen* screen = malloc(sizeof *screen);
    ERROR_EXIT_IF_NULL(screen, "Screen_Create(): screen is NULL");

    screen->draw_buffer = calloc((rows * columns), sizeof(char));
    ERROR_EXIT_IF_NULL(screen->draw_buffer, "Screen_Create(): screen->draw_buffer is NULL");

    screen->print_buffer = calloc((rows * columns) + rows, sizeof(char));
    ERROR_EXIT_IF_NULL(screen->print_buffer, "Screen_Create(): screen->print_buffer is NULL");

    screen->rows    = rows;
    screen->columns = columns;

    return screen;
}


void
Screen_Render(struct Screen* screen)
{
    int print_buffer_index = 0;
    int j = 0;

    /* Return the cursor to the top left corner */
    printf("\033[H");

    for (int i = 0; i < screen->columns * screen->rows; ++i) {
        ++j;

        if (screen->draw_buffer[i] == '\0')
            screen->print_buffer[print_buffer_index] = NONE_SPRITE;
        else
            screen->print_buffer[print_buffer_index] = screen->draw_buffer[i];

        if (j == screen->columns) {
            screen->print_buffer[++print_buffer_index] = '\n';
            j = 0;
        }

        ++print_buffer_index;
    }

    printf("%s", screen->print_buffer);
}


void
Screen_PushToPos(struct Screen* screen,
                 int  column,
                 int  row,
                 char ch)
{
    screen->draw_buffer[POS_TO_INDEX(screen->columns, column, row)] = ch;
}


void
Screen_ClearPos(struct Screen* screen,
                int  column,
                int  row)
{
    screen->draw_buffer[POS_TO_INDEX(screen->columns, column, row)] = '\0';
}


void
GameObject_Destroy(struct GameObject* gameobject)
{
    free(gameobject);
}


struct GameObject*
GameObject_Create(int x,
                  int y,
                  int type,
                  char sprite)
{
    struct GameObject* gameobject = malloc(sizeof *gameobject);
    ERROR_EXIT_IF_NULL(gameobject, "GameObject_Create(): gameobject is NULL");

    gameobject->x      = x;
    gameobject->y      = y;
    gameobject->type   = type;
    gameobject->sprite = sprite;

    return gameobject;
}


void
GameObject_SetPos(struct GameObject* gameobject,
                  int x,
                  int y)
{
    gameobject->x = x;
    gameobject->y = y;
}


void
GameObject_AddPos(struct GameObject* gameobject,
                  int offset_x,
                  int offset_y)
{
    GameObject_SetPos(gameobject, gameobject->x + offset_x, gameobject->y + offset_y);
}


int
GameObject_InBounds(struct GameObject* gameobject,
                    int max_x,
                    int max_y)
{
    return (gameobject->x < max_x && gameobject->y < max_y
        &&  gameobject->x > -1    && gameobject->y > -1);
}


void
Snake_Destroy(struct Snake* snake)
{
    for (int i = 0; i < snake->length; ++i)
        GameObject_Destroy(snake->parts[i]);

    free(snake->parts);
    free(snake);
}


struct Snake*
Snake_Create(int rows,
             int columns)
{
    struct Snake* snake = malloc(sizeof *snake);
    ERROR_EXIT_IF_NULL(snake, "Snake_Create(): snake is NULL");

    snake->parts = malloc(sizeof(*snake->parts) * (rows * columns));
    ERROR_EXIT_IF_NULL(snake->parts, "Snake_Create(): snake->parts is NULL");

    return snake;
}


/* Initializes the snake */
void
Snake_Init(struct Snake* snake,
           int start_x,
           int start_y)
{
    snake->parts[0]     = GameObject_Create(start_x, start_y, SNAKE_OBJTYPE, SNAKE_SPRITE);
    snake->head         = snake->parts[0];
    snake->tail         = snake->head;
    snake->length       = 1;
    snake->direction    = SNAKE_DIR_DOWN;
}


void
Snake_SetDirection(struct Snake* snake,
                   char direction)
{
    snake->direction = direction;
}


void
Snake_Move(struct Snake* snake,
           char direction)
{
    for (int i = snake->length - 1; i > 0; --i) {
        snake->parts[i]->x = snake->parts[i - 1]->x;
        snake->parts[i]->y = snake->parts[i - 1]->y;
    }

    switch (direction)
    {
        case SNAKE_DIR_UP:
            GameObject_AddPos(snake->head, 0, -1);
            break;

        case SNAKE_DIR_DOWN:
            GameObject_AddPos(snake->head, 0, 1);
            break;

        case SNAKE_DIR_LEFT:
            GameObject_AddPos(snake->head, -1, 0);
            break;

        case SNAKE_DIR_RIGHT:
            GameObject_AddPos(snake->head, 1, 0);
            break;

        default:
            ERROR_EXIT("Snake_Move(): Invalid Direction");
    }
}


void
Snake_Grow(struct Snake* snake)
{
    snake->parts[snake->length] = GameObject_Create(snake->tail->x, snake->tail->y,
                                                    SNAKE_OBJTYPE, SNAKE_SPRITE);

    snake->tail = snake->parts[snake->length++];
}


/* De-allocates everything allocated by the game */
void
SnakeGame_Destroy(struct SnakeGame* snakegame)
{
    Snake_Destroy(snakegame->snake);
    Screen_Destroy(snakegame->screen);
    InputHandler_Destroy(snakegame->inputhandler);
    GameObject_Destroy(snakegame->food);

    free(snakegame->grid);
    free(snakegame);
}


void
SnakeGame_DrawGameObject(struct Screen* screen,
                         struct GameObject* gameobject)
{
    Screen_PushToPos(screen, gameobject->x, gameobject->y, gameobject->sprite);
}


/* Creates (allocates memory for) the necessary components of the game */
struct SnakeGame*
SnakeGame_Create(int grid_rows,
                 int grid_columns)
{
    struct SnakeGame* snakegame = malloc(sizeof *snakegame);
    ERROR_EXIT_IF_NULL(snakegame, "SnakeGame_Create(): snakegame is NULL");

    snakegame->grid = calloc(grid_rows * grid_columns, sizeof(char));
    ERROR_EXIT_IF_NULL(snakegame->grid, "SnakeGame_Create(): snakegame->grid is NULL");

    /* These all have NULL checks in their respective x_Create functions */
    snakegame->snake        = Snake_Create(grid_rows, grid_columns);
    snakegame->screen       = Screen_Create(grid_rows, grid_columns);
    snakegame->inputhandler = InputHandler_Create();
    snakegame->food         = GameObject_Create(0, 0, FOOD_OBJTYPE, FOOD_SPRITE);

    return snakegame;
}


/* Returns the GameObject type of whatever is under the snake's head, and NONE_OBJTYPE
   if nothing. Must be called *before* writing the snake objtype to the tile */
char
SnakeGame_GetCollision(struct SnakeGame* snakegame)
{
    return snakegame->grid[POS_TO_INDEX(snakegame->screen->columns,
                                        snakegame->snake->head->x,
                                        snakegame->snake->head->y)];
}


int
SnakeGame_GetInBounds(struct SnakeGame* snakegame)
{
    return GameObject_InBounds(snakegame->snake->head,
                               snakegame->screen->columns,
                               snakegame->screen->rows);
}


/* Reads input and sets the direction of the snake accordingly, while
   disallowing 180 degree turns (e.g. no going directly from down to up). */
void
SnakeGame_ProcessInput(struct SnakeGame* snakegame)
{
    if (InputHandler_KeyPressed()) {
        switch (InputHandler_GetChar())
        {
            case 'w':
                if (snakegame->snake->direction != SNAKE_DIR_DOWN)
                    Snake_SetDirection(snakegame->snake, SNAKE_DIR_UP);
                break;

            case 'a':
                if (snakegame->snake->direction != SNAKE_DIR_RIGHT)
                    Snake_SetDirection(snakegame->snake, SNAKE_DIR_LEFT);
                break;

            case 's':
                if (snakegame->snake->direction != SNAKE_DIR_UP)
                    Snake_SetDirection(snakegame->snake, SNAKE_DIR_DOWN);
                break;

            case 'd':
                if (snakegame->snake->direction != SNAKE_DIR_LEFT)
                    Snake_SetDirection(snakegame->snake, SNAKE_DIR_RIGHT);
                break;
        }
    }
}


void
SnakeGame_SpawnFood(struct SnakeGame* snakegame)
{
    int index;

    while (1) {
        snakegame->food->x = RANDINT(0, snakegame->screen->columns - 1);
        snakegame->food->y = RANDINT(0, snakegame->screen->rows - 1);

        index = POS_TO_INDEX(snakegame->screen->columns, snakegame->food->x, snakegame->food->y);

        if (snakegame->grid[index] == NONE_OBJTYPE) {
            snakegame->grid[index] = FOOD_OBJTYPE;
            return;
        }
    }
}


void
SnakeGame_ConsumeFood(struct SnakeGame* snakegame)
{
    snakegame->grid[POS_TO_INDEX(snakegame->screen->columns,
                                 snakegame->food->x,
                                 snakegame->food->y)] = NONE_OBJTYPE;

    Snake_Grow(snakegame->snake);
}


int
SnakeGame_SnakeIsMaxLength(struct SnakeGame* snakegame)
{
    return snakegame->snake->length == (snakegame->screen->columns
                                        * snakegame->screen->rows) - 1;
}


/*
 * The game loop, most of the "high level" snake functionality is implemented here
 * it's a bit messy, but I've kept it this way since I think it very clearly conveys
 * the gameplay logic, and is also the 'core' function of the game, with the job of
 * managing all the *other* components, more or less.
 */
void
SnakeGame_GameLoop(struct SnakeGame* snakegame)
{
    while (1) {
        SLEEP_MS(GAME_TICK_INTERVAL);

        if (SnakeGame_SnakeIsMaxLength(snakegame)) {
            SnakeGame_MACRO_PrintAndTerminate(snakegame, "Victory, very cool!\n");
        }

        if (!SnakeGame_GetInBounds(snakegame)) {
            SnakeGame_MACRO_PrintAndTerminate(
                snakegame, "Unfortunate, the snake was %d tiles long.\n", snakegame->snake->length);
        }

        switch (SnakeGame_GetCollision(snakegame))
        {
            case SNAKE_OBJTYPE:
                SnakeGame_MACRO_PrintAndTerminate(
                    snakegame, "Unfortunate, the snake was %d tiles long.\n", snakegame->snake->length);

            case FOOD_OBJTYPE:
                SnakeGame_ConsumeFood(snakegame);
                SnakeGame_SpawnFood(snakegame);
                break;

            case NONE_OBJTYPE:
                break;

            default:
                ERROR_EXIT("SnakeGame_GameLoop(): Unknown collision type");
        }

        SnakeGame_ProcessInput(snakegame);

        /* Add each part of the snake to the grid and throw it into the render buffer */
        for (int i = 0; i < snakegame->snake->length; ++i) {
            snakegame->grid[POS_TO_INDEX(snakegame->screen->columns,
                                         snakegame->snake->parts[i]->x,
                                         snakegame->snake->parts[i]->y)] = SNAKE_OBJTYPE;

            SnakeGame_DrawGameObject(snakegame->screen, snakegame->snake->parts[i]);
        }

        SnakeGame_DrawGameObject(snakegame->screen, snakegame->food);
        Screen_Render(snakegame->screen);

        /* Remove the tail (last part of the snake) from the grid */
        snakegame->grid[POS_TO_INDEX(snakegame->screen->columns,
                                     snakegame->snake->tail->x,
                                     snakegame->snake->tail->y)] = NONE_OBJTYPE;

        /* ...and from the render buffer */
        Screen_ClearPos(snakegame->screen, snakegame->snake->tail->x, snakegame->snake->tail->y);
        Snake_Move(snakegame->snake, snakegame->snake->direction);
    }
}


/* Initializes the game variables and starts the main gameplay loop. */
void
SnakeGame_BeginPlay(struct SnakeGame* snakegame)
{
    srand(time(0));

    Snake_Init(snakegame->snake,
               snakegame->screen->columns / 2 - 1,
               snakegame->screen->rows / 2);

    Snake_Grow(snakegame->snake);
    Snake_Grow(snakegame->snake);

    SnakeGame_SpawnFood(snakegame);
    SnakeGame_GameLoop(snakegame);
}


int
main(void)
{
    system("clear");
    struct SnakeGame* snakegame = SnakeGame_Create(GAMEGRID_ROWS, GAMEGRID_COLUMNS);
    SnakeGame_BeginPlay(snakegame);

    return 0;
}
