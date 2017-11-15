#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TRUE 1
#define FALSE 0
#define DO_NOT_EXIST -1
#define UP 1
#define RIGHT 1
#define DOWN -1
#define LEFT -1

#define FIELD_WIDTH 96
#define FIELD_LENGTH 128
#define SUBFIELD_WIDTH 32
#define SUBFIELD_LENGTH 32

#define ROUNDS 2700
#define TEAMS 2
#define PROCS 34
#define FIELDS 12
#define PLAYERS 22
#define PLAYERS_PER_TEAM 11

#define PLAYER_STAT_MAX 10
#define PLAYER_ALL_MAX 15

#define COMM_FIELDS 0
#define COMM_A 1
#define COMM_B 2

#define TEAM_A 0
#define TEAM_B 1

/* ==================== STRUCTS ====================*/
typedef struct {
    int x, y;
} Ball;

typedef struct {
    int prevX, prevY, currX, currY;
    int team, reached, kicked, challenge;
    int speed, dribble, kick;
} Player;

typedef struct {
    Ball ball;
    Player players[PLAYERS];
} Field;

/* ===================== UTILS =====================*/
// 0-11: Field processes
// 12-22: Team A players
// 23-33: Team B players
int isField(int rank) {
    return rank >= 0 && rank <= 11 ? TRUE : FALSE;
}

int isTeamA(int rank) {
    return rank >= 12 && rank <= 22 ? TRUE : FALSE;
}

int isTeamB(int rank) {
    return rank >= 23 && rank <= 33 ? TRUE : FALSE;
}

int playerIsInField(Field *field, int playerRank) {
    return 
        field->players[playerRank - FIELDS].currX != DO_NOT_EXIST && 
        field->players[playerRank - FIELDS].currY != DO_NOT_EXIST ? TRUE : FALSE;
}

int ballIsInField(Field *field) {
    return field->ball.x != DO_NOT_EXIST && field->ball.y != DO_NOT_EXIST ? TRUE : FALSE;
}

int getFieldRankFromCoords(int x, int y) {
    int numRows = FIELD_WIDTH / SUBFIELD_WIDTH;
    int numCols = FIELD_LENGTH / SUBFIELD_LENGTH;
    int row = y / SUBFIELD_WIDTH;
    int col = x / SUBFIELD_LENGTH;
    // printf("row=%d, col=%d\n", row, col);
    return col + row * numCols;
}

int bothPointsInRange(int x1, int y1, int x2, int y2, int distance) {
    int horizontalDistance = abs(x1 - x2);
    int verticalDistance = abs(y1 - y2);
    return horizontalDistance + verticalDistance <= distance ? TRUE : FALSE;
}

/* ====================== INIT ======================*/
void initField(int rank, Field *field) {
    // Initialize ball position in the center at (64, 48)
    int fieldRank = getFieldRankFromCoords(FIELD_LENGTH / 2, FIELD_WIDTH / 2);
    if (rank == fieldRank) {
        field->ball.x = FIELD_LENGTH / 2;
        field->ball.y = FIELD_WIDTH / 2;
    } else {
        field->ball.x = DO_NOT_EXIST;
        field->ball.y = DO_NOT_EXIST;
    }

    // Initialize all player positions to DO_NOT_EXIST
    int p;
    for (p = 0; p < PLAYERS; p++) {
        field->players[p].prevX = DO_NOT_EXIST;
        field->players[p].prevY = DO_NOT_EXIST;
        field->players[p].currX = DO_NOT_EXIST;
        field->players[p].currY = DO_NOT_EXIST;
    }
}

void initPlayer(int rank, Player *player) {
    // Initialize player positions randomly
    player->currX = player->prevX = rand() % FIELD_LENGTH;
    player->currY = player->prevY = rand() % FIELD_WIDTH;
    player->team = isTeamA(rank) ? TEAM_A : TEAM_B;
    player->reached = player->kicked = 0;
    player->challenge = -1;

    // Initialize player stats randomly
    player->speed = player->dribble = player->kick = 1;

    int stats = PLAYER_ALL_MAX;
    stats -= (player->speed + player->dribble + player->kick);
    int increment;
    increment = rand() % PLAYER_STAT_MAX;
    player->speed += increment;
    stats -= increment;
    increment = rand() % stats;
    player->dribble += increment;
    stats -= increment;
    player->kick += stats;
}

void printField(int rank, Field *field) {
    if (isField(rank)) {
        if (ballIsInField(field)) {
            printf("[Process %d] Ball position: (%d, %d)\n", rank, field->ball.x, field->ball.y);
        }
        int p;
        for (p = 0; p < PLAYERS; p++) {
            int playerRank = p + FIELDS;
            if (playerIsInField(field, playerRank)) {
                printf("[Process %d] Player %d position: (%d, %d), team %d, reached=%d, kicked=%d, challenge=%d, speed=%d, dribble=%d, kick=%d\n", 
                rank, playerRank, field->players[p].currX, field->players[p].currY, 
                field->players[p].team, field->players[p].reached, 
                field->players[p].kicked, field->players[p].challenge, 
                field->players[p].speed, field->players[p].dribble, field->players[p].kick);
            }
        }
    }
}

/* ================ COLLECTIVE FUNCTIONS ================*/
void updatePlayerPositions(int rank, Field *field, Ball *ball, Player *player) {
    int newPosition[2];

    int p;
    for (p = 0; p < PLAYERS; p++) {
        int root = p + FIELDS;
        if (!isField(rank)) {
            newPosition[0] = player->currX;
            newPosition[1] = player->currY;
        }

        MPI_Bcast(&newPosition, 2, MPI_INT, root, MPI_COMM_WORLD);
        if (isField(rank)) {
            // printf("field process %d received (%d, %d) from %d\n", rank, newPosition[0], newPosition[1], root);
            // For every field process, check if the root player process already exists
            // in the current field process
            if (playerIsInField(field, root)) {
                field->players[p].currX = DO_NOT_EXIST;
                field->players[p].currY = DO_NOT_EXIST;
            }

            // Ignore the broadcast if the position sent is not within this field
            int fieldRank = getFieldRankFromCoords(newPosition[0], newPosition[1]);
            if (rank == fieldRank) {
                // printf("player %d (%d, %d) is now in field %d\n", root, newPosition[0], newPosition[1], rank);
                field->players[p].prevX = field->players[p].currX;
                field->players[p].prevY = field->players[p].currY;
                field->players[p].currX = newPosition[0];
                field->players[p].currY = newPosition[1];
            }
        }
    }
}

void updatePlayerData(int rank, Field *field, Ball *ball, Player *player) {
    int newData[7];

    int p;
    for (p = 0; p < PLAYERS; p++) {
        int root = p + FIELDS;
        if (!isField(rank)) {
            newData[0] = player->team;
            newData[1] = player->reached;
            newData[2] = player->kicked;
            newData[3] = player->challenge;
            newData[4] = player->speed;
            newData[5] = player->dribble;
            newData[6] = player->kick;
        }

        MPI_Bcast(&newData, 7, MPI_INT, root, MPI_COMM_WORLD);
        if (isField(rank)) {
            if (playerIsInField(field, root)) {
                field->players[p].team = newData[0];
                field->players[p].reached = newData[1];
                field->players[p].kicked = newData[2];
                field->players[p].challenge = newData[3];
                field->players[p].speed = newData[4];
                field->players[p].dribble = newData[5];
                field->players[p].kick = newData[6];
            }
        }
    }
}

void updateBallPositions(int rank, Field *field, Ball *ball, Player *player) {
    int newPosition[2];

    int p;
    for (p = 0; p < PLAYERS; p++) {
        int root = p + FIELDS;
        if (!isField(rank)) {
            if (player->challenge == 1) {

            }
        }
    }
}

void broadcastBallPosition(int rank, Field *field, Ball *ball, Player *player) {
    int ballPosition[2];

    int f;
    for (f = 0; f < FIELDS; f++) {
        if (isField(rank)) {
            ballPosition[0] = field->ball.x;
            ballPosition[1] = field->ball.y;
        }

        MPI_Bcast(&ballPosition, 2, MPI_INT, f, MPI_COMM_WORLD);

        // Only update ball position for players if not DO_NOT_EXIST
        if (!isField(rank)) {
            if (ballPosition[0] != DO_NOT_EXIST && 
                ballPosition[1] != DO_NOT_EXIST) {
                ball->x = ballPosition[0];
                ball->y = ballPosition[1];
                // printf("player %d knows the ball is at (%d, %d)\n", rank, ball->x, ball->y);
            }
        }
    }
}

/* =============== PLAYER FUNCTIONS ================*/
void clearPlayerData(int rank, Player *player) {
    if (!isField(rank)) {
        player->reached = player->kicked = 0;
        player->challenge = -1;
    }
}

void movePlayersTowardsBall(int rank, Ball *ball, Player *player) {
    // Movement rules
    // 1. Stop when ball is reached, or
    // 2. Moved n squares, where n = speed skill
    // 3. Always in the direction of the ball
    
    if (!isField(rank)) {
        // If the ball is within player->speed squares, move the player to the same square as the ball
        if (bothPointsInRange(ball->x, ball->y, player->currX, player->currY, player->speed)) {
            player->prevX = player->currX;
            player->prevY = player->currY;
            player->currX = ball->x;
            player->currY = ball->y;
            player->reached = 1;
        }

        // Determine direction to travel towards ball, and move a random combined distance of
        // player->speed squares in both directions
        int horizontalDirection = (ball->x - player->currX) > 0 ? RIGHT : LEFT;
        int verticalDirection = (ball->y - player->currY) > 0 ? UP : DOWN;
        int horizontalDistance = rand() % (player->speed + 1);
        int verticalDistance = player->speed - horizontalDistance;
        player->prevX = player->currX;
        player->prevY = player->currY;
        player->currX += horizontalDistance * horizontalDirection;
        player->currY += verticalDistance * verticalDirection;

        // Make sure the player does not go out of bounds
        if (player->currX < 0) {
            player->currX = 0;
        }
        if (player->currY < 0) {
            player->currY = 0;
        }
        if (player->currX >= FIELD_LENGTH) {
            player->currX = FIELD_LENGTH - 1;
        }
        if (player->currY >= FIELD_WIDTH) {
            player->currY = FIELD_WIDTH - 1;
        }

        printf("player %d (%d, %d) => (%d, %d) with speed=%d\n", rank, player->prevX, player->prevY, player->currX, player->currY, player->speed);
    }
}

/* ======================== MAIN =========================*/
int main(int argc, char *argv[]) {
    // MPI Initialization
    int rank, p, commRank, commSize;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    srand(time(0) + rank);

    // Split processes into appropriate communicators
    MPI_Comm COMM;
    if (isField(rank)) {
        MPI_Comm_split(MPI_COMM_WORLD, COMM_FIELDS, rank, &COMM);
    }
    if (isTeamA(rank)) {
        MPI_Comm_split(MPI_COMM_WORLD, COMM_A, rank, &COMM);
    }
    if (isTeamB(rank)) {
        MPI_Comm_split(MPI_COMM_WORLD, COMM_B, rank, &COMM);
    }

    MPI_Comm_rank(COMM, &commRank);
    MPI_Comm_size(COMM, &commSize);

    // Initialize private data for each of the processes
    Field field;
    Player player;
    Ball ball;
    if (isField(rank)) {
        initField(rank, &field);
    } else {
        initPlayer(rank, &player);
    }

    // Wait for all initializations to finish
    MPI_Barrier(MPI_COMM_WORLD);
    // printField(rank, &field);

    // Broadcast all player initial positions to subfields
    updatePlayerPositions(rank, &field, &ball, &player);
    updatePlayerData(rank, &field, &ball, &player);

    // Run for n rounds
    int r;
    for (r = 0; r < 1; r++) {
        clearPlayerData(rank, &player);
        broadcastBallPosition(rank, &field, &ball, &player);
        movePlayersTowardsBall(rank, &ball, &player);

        // Wait for all player movement to finish
        MPI_Barrier(MPI_COMM_WORLD);

        // printField(rank, &field);
    }

    MPI_Finalize();

    return 0;
}