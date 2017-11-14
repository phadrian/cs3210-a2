#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_PROCS 12
#define NUM_ROUNDS 900
#define NUM_PLAYERS 11

#define FIELD_PROC 0
#define FIELD_WIDTH 64
#define FIELD_LENGTH 128

#define PLAYER_DIST 10
#define PLAYER_REACHED_BALL 1
#define PLAYER_WON_BALL 1
#define PLAYER_LOST_BALL 0

#define UP 1
#define RIGHT 1
#define DOWN -1
#define LEFT -1

/* ==================== STRUCTS ====================*/
typedef struct {
    int x, y;
} Ball;

typedef struct {
    int reached, kicked;
} RoundData;

typedef struct {
    int x, y, distance, reaches, kicks;
    RoundData roundData;
} Player;

typedef struct {
    Ball ball;
    Player players[NUM_PLAYERS];
} Field;

/* ================= INIT FUNCTIONS =================*/
void initField(Field *field) {
    // Initialize ball position to center of field
    field->ball.x = FIELD_LENGTH / 2;
    field->ball.y = FIELD_WIDTH / 2;

    // Initialize all player positions to 0
    int p;
    for (p = 0; p < NUM_PLAYERS; p++) {
        field->players[p].x = 0;
        field->players[p].y = 0;
    }
}

void initPlayer(Player *player) {
    // Initialize player position randomly
    player->x = rand() % FIELD_LENGTH;
    player->y = rand() % FIELD_WIDTH;
    player->distance = player->reaches = player->kicks = 0;
    player->roundData.reached = player->roundData.kicked = PLAYER_LOST_BALL;
}

void printField(Field *field) {
    printf("===================== FIELD INFO =====================\n");
    printf("Ball position: (%d, %d)\n", field->ball.x, field->ball.y);
    int p;
    for (p = 1; p <= NUM_PLAYERS; p++) {
        printf("Player %d position: (%d, %d)\n", p, field->players[p - 1].x, field->players[p - 1].y);
        printf("dist=%d, reaches=%d, kicks=%d, reached=%d, kicked=%d\n", 
            field->players[p - 1].distance, field->players[p - 1].reaches, field->players[p - 1].kicks, 
            field->players[p - 1].roundData.reached, field->players[p - 1].roundData.kicked);
    }
    printf("======================================================\n");
}

/* ================ FIELD FUNCTIONS ================*/
void fieldGetPositions(Field *field) {
    int newPositions[NUM_PROCS][2];
    MPI_Request reqs[NUM_PLAYERS];
    MPI_Status stats[NUM_PLAYERS];

    // Start non-blocking receives
    int p;
    for (p = 1; p <= NUM_PLAYERS; p++) {
        MPI_Irecv(&newPositions[p], 2, MPI_INT, p, p, MPI_COMM_WORLD, &reqs[p - 1]);
    }
    MPI_Waitall(NUM_PLAYERS, reqs, stats);

    for (p = 1; p <= NUM_PLAYERS; p++) {
        field->players[p - 1].x = newPositions[p][0];
        field->players[p - 1].y = newPositions[p][1];
    }
}

void fieldSendBallPositions(Field *field) {
    int ballPosition[2];
    ballPosition[0] = field->ball.x;
    ballPosition[1] = field->ball.y;
    MPI_Request reqs[NUM_PLAYERS];
    MPI_Status stats[NUM_PLAYERS];

    int p;
    for (p = 1; p <= NUM_PLAYERS; p++) {
        MPI_Isend(&ballPosition, 2, MPI_INT, p, p, MPI_COMM_WORLD, &reqs[p - 1]);
    }
    MPI_Waitall(NUM_PLAYERS, reqs, stats);
}

void fieldSendKickSelection(Field *field) {
    int kickSelection[NUM_PROCS];
    MPI_Request reqs[NUM_PLAYERS];
    MPI_Status stats[NUM_PLAYERS];
    
    // Mark players who have reached the same square as the ball
    int p;
    int playersAtBallPosition = 0;
    for (p = 1; p <= NUM_PLAYERS; p++) {
        if (field->players[p - 1].x == field->ball.x && 
            field->players[p - 1].y == field->ball.y) {
            kickSelection[p] = PLAYER_WON_BALL;
            playersAtBallPosition++;
        } else {
            kickSelection[p] = PLAYER_LOST_BALL;
        }
    }

    // Handle random selection for ball winning
    if (playersAtBallPosition > 1) {
        int selectedPlayer = rand() % playersAtBallPosition;
        int playerIndex = 0;
        for (p = 1; p <= NUM_PLAYERS; p++) {
            if (kickSelection[p] == PLAYER_WON_BALL) {
                if (playerIndex != selectedPlayer) {
                    kickSelection[p] = PLAYER_LOST_BALL;
                }
                playerIndex++;
            }
        }
    }

    // Send kick selections
    for (p = 1; p <= NUM_PLAYERS; p++) {
        MPI_Isend(&kickSelection[p], 1, MPI_INT, p, p, MPI_COMM_WORLD, &reqs[p - 1]);
    }
    MPI_Waitall(NUM_PLAYERS, reqs, stats);
}

void fieldGetKickResult(Field *field) {
    int newBallPositions[NUM_PROCS][3];
    MPI_Request reqs[NUM_PLAYERS];
    MPI_Status stats[NUM_PLAYERS];

    int p;
    for (p = 1; p <= NUM_PLAYERS; p++) {
        MPI_Irecv(&newBallPositions[p], 3, MPI_INT, p, p, MPI_COMM_WORLD, &reqs[p - 1]);
    }
    MPI_Waitall(NUM_PLAYERS, reqs, stats);

    // Only update the ball location if it has been kicked
    for (p = 1; p <= NUM_PLAYERS; p++) {
        if (newBallPositions[p][2] == PLAYER_WON_BALL) {
            field->ball.x = newBallPositions[p][0];
            field->ball.y = newBallPositions[p][1];
            // printf("Ball is now at (%d, %d)\n", field->ball.x, field->ball.y);
            break;
        }
    }
}

void fieldGetRoundData(Field *field) {
    int roundData[NUM_PROCS][5];
    MPI_Request reqs[NUM_PLAYERS];
    MPI_Status stats[NUM_PLAYERS];

    int p;
    for (p = 1; p <= NUM_PLAYERS; p++) {
        MPI_Irecv(&roundData[p], 5, MPI_INT, p, p, MPI_COMM_WORLD, &reqs[p - 1]);
    }
    MPI_Waitall(NUM_PLAYERS, reqs, stats);

    // Update all the round data for all players
    for (p = 1; p <= NUM_PLAYERS; p++) {
        field->players[p - 1].distance = roundData[p][0];
        field->players[p - 1].reaches = roundData[p][1];
        field->players[p - 1].kicks = roundData[p][2];
        field->players[p - 1].roundData.reached = roundData[p][3];
        field->players[p - 1].roundData.kicked = roundData[p][4];
    }
}

/* =============== PLAYER FUNCTIONS ================*/
void playerSendPosition(int rank, int x, int y) {
    int newPosition[2];
    newPosition[0] = x;
    newPosition[1] = y;

    MPI_Request req;
    MPI_Status stats;

    MPI_Isend(&newPosition, 2, MPI_INT, FIELD_PROC, rank, MPI_COMM_WORLD, &req);
    MPI_Wait(&req, &stats);
}

void playerGetBallPosition(int rank, Ball *ball) {
    int ballPosition[2];
    MPI_Request req;
    MPI_Status stats;

    MPI_Irecv(&ballPosition, 2, MPI_INT, FIELD_PROC, rank, MPI_COMM_WORLD, &req);
    MPI_Wait(&req, &stats);

    // Update player's internal ball position
    ball->x = ballPosition[0];
    ball->y = ballPosition[1];

    // printf("player %d knows ball is at (%d, %d)\n", rank, ball->x, ball->y);
}

void playerMoveTowardsBall(int rank, Ball *ball, Player *player) {
    // Movement rules:
    // 1. Stop when ball is reached, or
    // 2. Moved 10m (assume no diagonal movement)
    // 3. Always move in the direction of ball

    // player->x = ball->x;
    // player->y = ball->y;

    // If the ball is within 10 squares of player, move player to same square as ball
    int horizontalDistanceToBall = abs(ball->x - player->x);
    int verticalDistanceToBall = abs(ball->y - player->y);
    if (verticalDistanceToBall + horizontalDistanceToBall <= PLAYER_DIST) {
        // printf("player %d (%d, %d) moving to ball(%d, %d)\n", rank, player->x, player->y, ball->x, ball->y);
        player->x = ball->x;
        player->y = ball->y;
        player->distance += (verticalDistanceToBall + horizontalDistanceToBall);
        player->reaches++;
        player->roundData.reached = PLAYER_REACHED_BALL;
        return;
    }

    // Determine direction to travel towards ball, and move a random combined distance of 
    // PLAYER_DIST squares in both directions
    // printf("player %d (%d, %d) moving to square", rank, player->x, player->y);
    int horizontalDirection = (ball->x - player->x) > 0 ? RIGHT : LEFT;
    int verticalDirection = (ball->y - player->y) > 0 ? UP : DOWN;
    int horizontalDistance = rand() % (PLAYER_DIST + 1);
    int verticalDistance = PLAYER_DIST - horizontalDistance;
    player->x += horizontalDistance * horizontalDirection;
    player->y += verticalDistance * verticalDirection;
    player->distance += PLAYER_DIST;

    // Check to make sure the player does not go out of bounds and compensate for the 
    // extra distance travelled if they do
    if (player->x < 0) {
        player->distance -= (0 - player->x);
        player->x = 0;
    }
    if (player->y < 0) {
        player->distance -= (0 - player->y);
        player->y = 0;
    }
    if (player->x >= FIELD_LENGTH) {
        player->distance -= (player->x - (FIELD_LENGTH - 1));
        player->x = FIELD_LENGTH - 1;
    }
    if (player->y >= FIELD_WIDTH) {
        player->distance -= (player->y - (FIELD_WIDTH - 1));
        player->y = FIELD_WIDTH - 1;
    }
    
    // printf("(%d, %d)\n", player->x, player->y);
}

void playerGetKickSelection(int rank, Ball *ball, Player *player) {
    int kickSelection;
    MPI_Request req;
    MPI_Status stats;

    MPI_Irecv(&kickSelection, 1, MPI_INT, FIELD_PROC, rank, MPI_COMM_WORLD, &req);
    MPI_Wait(&req, &stats);

    // printf("player %d's kick selection: %d\n", rank, kickSelection);

    // If player gets selected to kick, randomly relocate the ball on the field
    if (kickSelection == PLAYER_WON_BALL) {
        ball->x = rand() % FIELD_LENGTH;
        ball->y = rand() % FIELD_WIDTH;
        player->kicks++;
        player->roundData.kicked = PLAYER_WON_BALL;
    }
}

void playerSendKickResult(int rank, Ball *ball, Player *player) {
    int newBallPosition[3];
    newBallPosition[0] = ball->x;
    newBallPosition[1] = ball->y;
    newBallPosition[2] = player->roundData.kicked;

    MPI_Request req;
    MPI_Status stats;

    MPI_Isend(&newBallPosition, 3, MPI_INT, FIELD_PROC, rank, MPI_COMM_WORLD, &req);
    MPI_Wait(&req, &stats);
}

void playerSendRoundData(int rank, Ball *ball, Player *player) {
    int roundData[5];
    MPI_Request req;
    MPI_Status stats;

    roundData[0] = player->distance; 
    roundData[1] = player->reaches;
    roundData[2] = player->kicks;
    roundData[3] = player->roundData.reached;
    roundData[4] = player->roundData.kicked;

    MPI_Isend(&roundData, 5, MPI_INT, FIELD_PROC, rank, MPI_COMM_WORLD, &req);
    MPI_Wait(&req, &stats);
}

/* ======================= MAIN ========================*/
int main(int argc, char *argv[]) {
    // MPI initialization
    int numprocs, rank, p;

    // MPI_Request sendReqs[NUM_PLAYERS], recvReqs[NUM_PLAYERS];
    // MPI_Status sendStats[NUM_PLAYERS], recvStats[NUM_PLAYERS];

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    srand(time(0) + rank);

    // Initialize private data per process
    Field field, previousField;
    Player player;
    Ball ball;
    if (rank == FIELD_PROC) {
        initField(&field);
    } else {
        initPlayer(&player);
    }

    // Wait for all initialization to finish
    MPI_Barrier(MPI_COMM_WORLD);

    // Send/receive initial position data to/from player processes
    if (rank == FIELD_PROC) {
        fieldGetPositions(&field);
    } else {
        playerSendPosition(rank, player.x, player.y);
    }

    // Run for n rounds
    int r;
    for (r = 0; r < NUM_ROUNDS; r++) {
        // Update the previous field state
        if (rank == FIELD_PROC) {
            previousField.ball.x = field.ball.x;
            previousField.ball.y = field.ball.y;
            for (p = 0; p < NUM_PLAYERS; p++) {
                previousField.players[p].x = field.players[p].x;
                previousField.players[p].y = field.players[p].y;
                // previousField.players[p].distance = field.players[p].distance;
                // previousField.players[p].reaches = field.players[p].reaches;
                // previousField.players[p].kicks = field.players[p].kicks;
                // previousField.players[p].roundData.reached = field.players[p].roundData.reached;
                // previousField.players[p].roundData.kicked = field.players[p].roundData.kicked;
            }
        }

        if (rank == FIELD_PROC) {
            fieldSendBallPositions(&field);
        } else {
            // Reset the roundData for every new round
            player.roundData.reached = PLAYER_LOST_BALL;
            player.roundData.kicked = PLAYER_LOST_BALL;

            playerGetBallPosition(rank, &ball);
            playerMoveTowardsBall(rank, &ball, &player);
        }

        // Wait for all player movement to finish
        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == FIELD_PROC) {
            fieldGetPositions(&field);
            fieldSendKickSelection(&field);
            fieldGetKickResult(&field);
            fieldGetRoundData(&field);
        } else {
            playerSendPosition(rank, player.x, player.y);
            playerGetKickSelection(rank, &ball, &player);
            playerSendKickResult(rank, &ball, &player);
            playerSendRoundData(rank, &ball, &player);
        }

        // Ensure field is updated before proceeding to next round
        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == FIELD_PROC) {
            // printField(&field);
            printf("%d\n", r);
            printf("%d %d\n", field.ball.x, field.ball.y);
            for (p = 0; p < NUM_PLAYERS; p++) {
                printf("%d %d %d %d %d %d %d %d %d %d\n", p, previousField.players[p].x, previousField.players[p].y, 
                    field.players[p].x, field.players[p].y, 
                    field.players[p].roundData.reached, field.players[p].roundData.kicked, 
                    field.players[p].distance, field.players[p].reaches, field.players[p].kicks);
            }
            printf("\n");
        }
    }

    MPI_Finalize();

    return 0;
}