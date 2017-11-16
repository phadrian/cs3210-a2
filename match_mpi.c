#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

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

#define GOAL_LEFT_START_X 0
#define GOAL_LEFT_START_Y 43
#define GOAL_LEFT_END_X 0
#define GOAL_LEFT_END_Y 51
#define GOAL_RIGHT_START_X 127
#define GOAL_RIGHT_START_Y 43
#define GOAL_RIGHT_END_X 127
#define GOAL_RIGHT_END_Y 51

#define ROUNDS 900
#define TEAMS 2
#define PROCS 34
#define FIELDS 12
#define PLAYERS 22
#define PLAYERS_PER_TEAM 11

#define PLAYER_STAT_MAX 10
#define PLAYER_ALL_MAX 15
#define PLAYER_REACHED_BALL 1
#define PLAYER_NO_REACHED_BALL 0
#define PLAYER_KICKED_BALL 1
#define PLAYER_NO_KICKED_BALL 0
#define PLAYER_NO_CHALLENGE -1

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
    if (x == DO_NOT_EXIST || y == DO_NOT_EXIST) {
        return DO_NOT_EXIST;
    }

    int numRows = FIELD_WIDTH / SUBFIELD_WIDTH;
    int numCols = FIELD_LENGTH / SUBFIELD_LENGTH;
    int row = y / SUBFIELD_WIDTH;
    int col = x / SUBFIELD_LENGTH;
    // printf("row=%d, col=%d\n", row, col);
    return col + row * numCols;
}

int getDistanceBetweenPoints(int x1, int y1, int x2, int y2) {
    int horizontalDistance = abs(x1 - x2);
    int verticalDistance = abs(y1 - y2);
    return horizontalDistance + verticalDistance;
}

int bothPointsInRange(int x1, int y1, int x2, int y2, int distance) {
    return getDistanceBetweenPoints(x1, y1, x2, y2) <= distance ? TRUE : FALSE;
}

int getScoringDirection(Player *player, int round) {
    int scoreDirectionA = round < ROUNDS / 2 ? RIGHT : LEFT;
    int scoreDirectionB = scoreDirectionA == RIGHT ? LEFT : RIGHT;
    return player->team == TEAM_A ? scoreDirectionA : scoreDirectionB;
}

int goalScored(Ball *ball, Player *player, int round) {
    // For now ignore own goals, should not happen anyway
    int scoringDirection = getScoringDirection(player, round);
    int scoredAtLeft = 
        ball->x < GOAL_LEFT_START_X && 
        ball->y >= GOAL_LEFT_START_Y && 
        ball->y <= GOAL_LEFT_END_Y;
    int scoredAtRight =
        ball->x > GOAL_RIGHT_START_X &&
        ball->y >= GOAL_RIGHT_START_Y && 
        ball->y <= GOAL_RIGHT_END_Y;

    return scoredAtLeft || scoredAtRight ? TRUE : FALSE;
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
    player->reached = PLAYER_NO_REACHED_BALL;
    player->kicked = PLAYER_NO_KICKED_BALL;
    player->challenge = PLAYER_NO_CHALLENGE;

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
                printf("[Process %d] Player %d position: (%d, %d) => (%d, %d), team %d, reached=%d, kicked=%d, challenge=%d, speed=%d, dribble=%d, kick=%d\n", 
                rank, playerRank, 
                field->players[p].prevX, field->players[p].prevY,
                field->players[p].currX, field->players[p].currY, 
                field->players[p].team, field->players[p].reached, 
                field->players[p].kicked, field->players[p].challenge, 
                field->players[p].speed, field->players[p].dribble, field->players[p].kick);
            }
        }
    }
}

/* ================ COLLECTIVE FUNCTIONS ================*/
void updatePlayerPositions(int rank, Field *field, Ball *ball, Player *player) {
    int newPosition[4];

    int p;
    for (p = 0; p < PLAYERS; p++) {
        int root = p + FIELDS;
        if (!isField(rank)) {
            newPosition[0] = player->prevX;
            newPosition[1] = player->prevY;
            newPosition[2] = player->currX;
            newPosition[3] = player->currY;
        }

        MPI_Bcast(&newPosition, 4, MPI_INT, root, MPI_COMM_WORLD);
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
                field->players[p].prevX = newPosition[0];
                field->players[p].prevY = newPosition[1];
                field->players[p].currX = newPosition[2];
                field->players[p].currY = newPosition[3];
                // printf("field process %d player %d (%d, %d) => (%d, %d)\n", rank, root, field->players[p].prevX, field->players[p].prevY, field->players[p].currX, field->players[p].currY);
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

void updateBallPosition(int rank, Field *field, Ball *ball, Player *player) {
    int newPosition[2];

    int p;
    for (p = 0; p < PLAYERS; p++) {
        int root = p + FIELDS;
        if (!isField(rank) && player->kicked == PLAYER_KICKED_BALL) {
            newPosition[0] = ball->x;
            newPosition[1] = ball->y;
        } else {
            newPosition[0] = DO_NOT_EXIST;
            newPosition[1] = DO_NOT_EXIST;
        }

        MPI_Bcast(&newPosition, 2, MPI_INT, root, MPI_COMM_WORLD);

        // Ignore broadcasts from players that did not kick the ball
        if (newPosition[0] != DO_NOT_EXIST && newPosition[1] != DO_NOT_EXIST) {
            if (isField(rank)) {
                // For every field process, check if the ball location is already defined there
                if (ballIsInField(field)) {
                    field->ball.x = DO_NOT_EXIST;
                    field->ball.y = DO_NOT_EXIST;
                }

                // Ignore the broadcast if the new ball position is not within this field
                int fieldRank = getFieldRankFromCoords(newPosition[0], newPosition[1]);
                if (rank == fieldRank) {
                    field->ball.x = newPosition[0];
                    field->ball.y = newPosition[1];
                    printf("ball is now in field process %d at (%d, %d)\n", rank, field->ball.x, field->ball.y);
                }
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

void determineKicker(int rank, Field *field, Ball *ball, Player *player) {
    int challenge[PLAYERS];
    int selectedKicker = DO_NOT_EXIST;

    // Determine the ball challenge
    if (!isField(rank) && player->reached == PLAYER_REACHED_BALL) {
        player->challenge = (1 + rand() % 10) * player->dribble;
    }

    int p;
    for (p = 0; p < PLAYERS; p++) {
        int root = p + FIELDS;
        if (!isField(rank)) {
            challenge[p] = player->challenge;
        }

        // Broadcast all the players' ball challenges
        MPI_Bcast(&challenge, PLAYERS, MPI_INT, root, MPI_COMM_WORLD);
    }

    // Only the field process with the ball handles the ball challenges
    if (isField(rank) && ballIsInField(field)) {
        printf("ball is in field process %d\n", rank);
        printf("ball challenges: ");
        for (p = 0; p < PLAYERS; p++) {
            printf("%d ", challenge[p]);
        }
        printf("\n");
        int highestBallChallenge = 0;
        for (p = 0; p < PLAYERS; p++) {
            if (challenge[p] >= highestBallChallenge) {
                if (challenge[p] == highestBallChallenge) {
                    if (rand() % 11 > rand() % 11) {
                        selectedKicker = p + FIELDS;
                    }
                } else {
                    selectedKicker = p + FIELDS;
                }
                highestBallChallenge = challenge[p];
            }
        }
        printf("highest ball challenge=%d, selected kicker=player %d\n", highestBallChallenge, selectedKicker);
    }

    // Broadcast the selectedKicker to all players
    int f, kicker;
    for (f = 0; f < FIELDS; f++) {
        kicker = selectedKicker;
        MPI_Bcast(&kicker, 1, MPI_INT, f, MPI_COMM_WORLD);
        if (!isField(rank) && rank == kicker) {
            // printf("player %d selected as kicker\n", rank);
            player->kicked = PLAYER_KICKED_BALL;
        }
    }
}

void kickBall(int rank, Field *field, Ball *ball, Player *player, int round) {
    // Get positions of all teammates
    int positions[PLAYERS][2];
    int p;
    for (p = 0; p < PLAYERS; p++) {
        int root = p + FIELDS;
        if (!isField(rank)) {
            positions[p][0] = player->currX;
            positions[p][1] = player->currY;
        }

        MPI_Bcast(&positions[p], 2, MPI_INT, root, MPI_COMM_WORLD);
    }

    // Determine new ball position with priorities:
    // 1. Score into goal
    // 2. Kick to teammate within kick range
    // 3. Kick towards goal
    if (!isField(rank) && player->kicked == PLAYER_KICKED_BALL) {
        printf("player %d kicking ball\n", rank);
        int kickRange = player->kick * 2;
        int scoringDirection = getScoringDirection(player, round);
        int goalStartX, goalStartY, goalEndX, goalEndY;

        // Check if the goal is within kick range from current position
        int scoreFromTopGoalPost, scoreFromBottomGoalPost;
        if (scoringDirection == LEFT) {
            scoreFromTopGoalPost = bothPointsInRange(ball->x, ball->y, GOAL_LEFT_START_X - 1, GOAL_LEFT_START_Y, kickRange);
            scoreFromBottomGoalPost = bothPointsInRange(ball->x, ball->y, GOAL_LEFT_END_X - 1, GOAL_LEFT_END_Y, kickRange);
        } else {
            scoreFromTopGoalPost = bothPointsInRange(ball->x, ball->y, GOAL_RIGHT_START_X + 1, GOAL_RIGHT_START_Y, kickRange);
            scoreFromBottomGoalPost = bothPointsInRange(ball->x, ball->y, GOAL_RIGHT_END_X + 1, GOAL_RIGHT_END_Y, kickRange);
        }
        // Count as goal and reposition ball to center of field
        if (scoreFromTopGoalPost || scoreFromBottomGoalPost) {
            ball->x = FIELD_LENGTH / 2;
            ball->y = FIELD_WIDTH / 2;
            printf("player %d scored from (%d, %d) with kickrange=%d\n", rank, player->currX, player->currY, kickRange);
            return;
        }

        // Search for a teammate to pass to
        int teamStartIndex = player->team == TEAM_A ? 0 : PLAYERS_PER_TEAM;
        int teamEndIndex = teamStartIndex + PLAYERS_PER_TEAM - 1;
        for (p = teamStartIndex; p <= teamEndIndex; p++) {
            // Ignore ownself
            if (rank != p + FIELDS) {
                int teammateX = positions[p][0];
                int teammateY = positions[p][1];
                // Check whether teammate or ownself is closer to goal, and only pass the ball
                // to a teammate that is closer to the goal
                if (bothPointsInRange(player->currX, player->currY, teammateX, teammateY, kickRange)) {
                    if (scoringDirection == LEFT) {
                        int teammateDistanceToTopGoalPost = getDistanceBetweenPoints(GOAL_LEFT_START_X, GOAL_LEFT_START_Y, teammateX, teammateY);
                        int teammateDistanceToBottomGoalPost = getDistanceBetweenPoints(GOAL_LEFT_END_X, GOAL_LEFT_END_Y, teammateX, teammateY);
                        int ownDistanceToTopGoalPost = getDistanceBetweenPoints(GOAL_LEFT_START_X, GOAL_LEFT_START_Y, player->currX, player->currY);
                        int ownDistanceToBottomGoalPost = getDistanceBetweenPoints(GOAL_LEFT_END_X, GOAL_LEFT_END_Y, player->currX, player->currY);

                        int teammateDistanceToGoal = fmin(teammateDistanceToTopGoalPost, teammateDistanceToBottomGoalPost);
                        int ownDistanceToGoal = fmin(ownDistanceToTopGoalPost, ownDistanceToBottomGoalPost);
                        if (teammateDistanceToGoal < ownDistanceToGoal) {
                            ball->x = teammateX;
                            ball->y = teammateY;
                            printf("scoring left: player %d (%d, %d) passed to player %d (%d, %d)\n", rank, player->currX, player->currY, p + FIELDS, teammateX, teammateY);
                            return;
                        }
                    } else {
                        int teammateDistanceToTopGoalPost = getDistanceBetweenPoints(GOAL_RIGHT_START_X, GOAL_RIGHT_START_Y, teammateX, teammateY);
                        int teammateDistanceToBottomGoalPost = getDistanceBetweenPoints(GOAL_RIGHT_END_X, GOAL_RIGHT_END_Y, teammateX, teammateY);
                        int ownDistanceToTopGoalPost = getDistanceBetweenPoints(GOAL_RIGHT_START_X, GOAL_RIGHT_START_Y, player->currX, player->currY);
                        int ownDistanceToBottomGoalPost = getDistanceBetweenPoints(GOAL_RIGHT_END_X, GOAL_RIGHT_END_Y, player->currX, player->currY);

                        int teammateDistanceToGoal = fmin(teammateDistanceToTopGoalPost, teammateDistanceToBottomGoalPost);
                        int ownDistanceToGoal = fmin(ownDistanceToTopGoalPost, ownDistanceToBottomGoalPost);
                        if (teammateDistanceToGoal < ownDistanceToGoal) {
                            ball->x = teammateX;
                            ball->y = teammateY;
                            printf("scoring right: player %d (%d, %d) passed to player %d (%d, %d)\n", rank, player->currX, player->currY, p + FIELDS, teammateX, teammateY);
                            return;
                        }
                    }
                }
            }
        }

        // Kick the ball towards the goal
        int horizontalDistance = rand() % (kickRange + 1);
        int verticalDistance = kickRange - horizontalDistance;
        ball->x = player->currX + (horizontalDistance * scoringDirection);
        ball->y = player->currY + (verticalDistance * scoringDirection);

        // Handle cases when ball is kicked out of field, reposition in center
        if (ball->x < 0 || ball->x >= FIELD_LENGTH || ball->y < 0 || ball->y >= FIELD_WIDTH) {
            printf("ball kicked out of field (%d, %d), repositioning to center\n", ball->x, ball->y);
            ball->x = FIELD_LENGTH / 2;
            ball->y = FIELD_WIDTH / 2;
        }
        printf("ball is now at (%d, %d)\n", ball->x, ball->y);
    }
}

/* =============== PLAYER FUNCTIONS ================*/
void clearPlayerRoundData(int rank, Player *player) {
    if (!isField(rank)) {
        player->reached = PLAYER_NO_REACHED_BALL;
        player->kicked = PLAYER_NO_KICKED_BALL;
        player->challenge = PLAYER_NO_CHALLENGE;
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
            return;
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

        // printf("player %d (%d, %d) => (%d, %d) with speed=%d\n", rank, player->prevX, player->prevY, player->currX, player->currY, player->speed);
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
    for (r = 0; r < ROUNDS; r++) {
        clearPlayerRoundData(rank, &player);
        broadcastBallPosition(rank, &field, &ball, &player);
        movePlayersTowardsBall(rank, &ball, &player);

        // Wait for all player movement to finish
        MPI_Barrier(MPI_COMM_WORLD);
        // printField(rank, &field);

        // Update all the new player positions and round data
        updatePlayerPositions(rank, &field, &ball, &player);
        updatePlayerData(rank, &field, &ball, &player);

        // Handle ball kick
        determineKicker(rank, &field, &ball, &player);
        kickBall(rank, &field, &ball, &player, r);
        updateBallPosition(rank, &field, &ball, &player);

        // Ensure field is updated before proceeding to next round
        updatePlayerData(rank, &field, &ball, &player);
        MPI_Barrier(MPI_COMM_WORLD);
        printField(rank, &field);

        // Gather all the field data in field process 0 for output
        if (isField(rank)) {
            int data[TEAMS][PLAYERS_PER_TEAM][11];

            int sendBuffer[11];
            int receiveBuffer[FIELDS * 11];
            for (p = 0; p < PLAYERS; p++) {
                sendBuffer[0] = field.players[p].prevX;
                sendBuffer[1] = field.players[p].prevY;
                sendBuffer[2] = field.players[p].currX;
                sendBuffer[3] = field.players[p].currY;
                sendBuffer[4] = field.players[p].team;
                sendBuffer[5] = field.players[p].reached;
                sendBuffer[6] = field.players[p].kicked;
                sendBuffer[7] = field.players[p].challenge;
                sendBuffer[8] = field.players[p].speed;
                sendBuffer[9] = field.players[p].dribble;
                sendBuffer[10] = field.players[p].kick;

                MPI_Barrier(COMM);
                MPI_Gather(sendBuffer, 11, MPI_INT, receiveBuffer, 11, MPI_INT, 0, COMM);

                if (rank == 0) {
                    int players[FIELDS][11];
                    int i, row = 0, col = 0;
                    for (i = 0; i < FIELDS * 11; i++) {
                        players[row][col] = receiveBuffer[i];
                        col++;
                        if ((i + 1) % 11 == 0) {
                            row++;
                            col = 0;
                        }
                    }
                    // printf("player %d\n", p);
                    // for (row = 0; row < FIELDS; row++) {
                    //     for (col = 0; col < 11; col++) {
                    //         printf("%d ", players[row][col]);
                    //     }
                    //     printf("\n");
                    // }
                    int rowData[11];
                    for (row = 0; row < FIELDS; row++) {
                        int x = players[row][2];
                        int y = players[row][3];
                        if (x != DO_NOT_EXIST && y != DO_NOT_EXIST) {
                            for (i = 0; i < 11; i++) {
                                rowData[i] = players[row][i];
                            }
                            break;
                        }
                    }

                    // Store the row (player data) into the organized array
                    int team = rowData[4];
                    for (col = 0; col < 11; col++) {
                        data[team][p % 11][col] = rowData[col];
                    }
                }
            }

            if (rank == 0) {
                int t, i;
                printf("%d\n", r);
                for (t = 0; t < TEAMS; t++) {
                    for (p = 0; p < PLAYERS_PER_TEAM; p++) {
                        for (i = 0; i < 11; i++) {
                            printf("%d ", data[t][p][i]);
                        }
                        printf("\n");
                    }
                }
                printf("\n");
            }            
        }
    }

    MPI_Finalize();

    return 0;
}