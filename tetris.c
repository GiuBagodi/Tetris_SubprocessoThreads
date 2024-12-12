#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // Para usleep
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <termios.h>  // Para configurar modo nao-canonico
#include <sys/ioctl.h> // Para ioctl e FIONREAD
#include <sys/wait.h>  // Para wait

#define COLUNAS     12
#define LINHAS      18
#define INTERVALO_TICK 0.3
struct termios original_term;

typedef struct {
    int piece[4][4];
    int posX, posY;
} Piece;

const char* get_piece_color(int value) {
    switch (value) {
        case 1: return "\033[0;31;41m"; // Vermelho
        case 2: return "\033[0;32;42m"; // Verde
        case 3: return "\033[0;33;43m"; // Amarelo
        case 4: return "\033[0;34;44m"; // Azul
        case 5: return "\033[0;35;45m"; // Magenta
        case 6: return "\033[0;36;46m"; // Ciano
        default: return "\033[0;30;40m";   // Preto
    }
}

// Declaracoes de funcoes
void clear_screen();
void print_title();
void print_board();
void clear_piece();
void draw_piece();
void move_piece(int board[LINHAS][COLUNAS], int piece[4][4], int *x, int *y, char key);
char get_key();
int kbhit();
void enable_raw_mode();
void fix_piece();
int check_collision();
void rotate_piece();
void initializePieces();
int can_rotate();
int check_complete_lines();
void remove_line();
Piece generateRandomPiece();
int game_over();  // Declaracao da funcao game_over
void logica_jogo();

char tecla;
int fim = 0;

void *captura_tecla(void *arg) { 
    while (!fim) { 
        tecla = getchar();
        if (tecla == 'q'){
            fim = 1;
        } 
    } return NULL;
}



int main() {
    // Salvar o estado original do terminal
    enable_raw_mode();

    int board[LINHAS][COLUNAS] = {0};
    int pieces[7][4][4];
    initializePieces(pieces);
    Piece p = generateRandomPiece(pieces);
    int score = 0;

    // Criação dos pipes
    int pipe1[2], pipe2[2];
    if (pipe(pipe1) == -1 || pipe(pipe2) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        close(pipe1[0]); // Fecha o descritor de leitura do pipe1 no processo filho
        close(pipe2[1]); // Fecha o descritor de escrita do pipe2 no processo filho
        pthread_t thread_tecla;
        pthread_create(&thread_tecla, NULL, captura_tecla, NULL);
        logica_jogo(board, pieces, &p, &score, &fim, pipe1[1], pipe2[0]);
        pthread_join(thread_tecla, NULL);
        close(pipe1[1]); // Fecha o descritor de escrita do pipe1 no processo filho
        close(pipe2[0]); // Fecha o descritor de leitura do pipe2 no processo filho
        _exit(0);
    } else {
        close(pipe1[1]); // Fecha o descritor de escrita do pipe1 no processo pai
        close(pipe2[0]); // Fecha o descritor de leitura do pipe2 no processo pai
        char buffer[128];
        while (1) {
            read(pipe1[0], buffer, sizeof(buffer)); // Aguarda mensagem de game over
            clear_screen();
            printf("*********************************\n");
            printf("*                               *\n");
            printf("*        GAME OVER!             *\n");
            printf("*                               *\n");
            printf("*********************************\n");
            printf("\nPressione 'r' para reiniciar ou 'q' para sair.\n");

            char choice = getchar();
            if (choice == 'r') {
                write(pipe2[1], "restart", strlen("restart"));
            } else {
                write(pipe2[1], "exit", strlen("exit"));
                break;
            }
        }
        close(pipe1[0]); // Fecha o descritor de leitura do pipe1 no processo pai
        close(pipe2[1]); // Fecha o descritor de escrita do pipe2 no processo pai
        wait(NULL); // Espera o subprocesso terminar
    }

    return 0;
}


void logica_jogo(int board[LINHAS][COLUNAS], int pieces[7][4][4], Piece *p, int *score, int *fim, int write_pipe, int read_pipe) {
    clock_t inicio = clock();
    clock_t proximo_tick = inicio + INTERVALO_TICK * CLOCKS_PER_SEC;

    while (!(*fim)) {
        clock_t agora = clock();

        if ((double)(agora - proximo_tick) / CLOCKS_PER_SEC >= 0) {
            proximo_tick += INTERVALO_TICK * CLOCKS_PER_SEC;

            if (tecla == 'a' || tecla == 's' || tecla == 'd' || tecla == 'w') {
                move_piece(board, p->piece, &(p->posX), &(p->posY), tecla);
                tecla = 0;
            }

            clear_screen();
            draw_piece(board, p->piece, p->posX, p->posY);
            print_title();
            print_board(board);
            printf("\nPosicao atual: (%d, %d)\n", p->posX, p->posY);
            printf("\nScore: %d\n", *score);
            clear_piece(board, p->piece, p->posX, p->posY);

            int collision = check_collision(board, p->piece, p->posX, p->posY + 1);
            if (collision == 0) {
                p->posY++;
            } else {
                fix_piece(board, p->piece, p->posX, p->posY);
                int lines_removed = check_complete_lines(board, score);
                if (lines_removed > 0) {
                    printf("%d linha(s) removida(s)!\n", lines_removed);
                }
                if (game_over(board)) {
                    printf("Game Over!\n");
                    write(write_pipe, "Game Over", strlen("Game Over")); // Envia mensagem de game over
                    char response[10];
                    read(read_pipe, response, sizeof(response)); // Aguarda resposta do processo pai
                    if (strcmp(response, "restart") == 0) {
                        for (int i = 0; i < LINHAS; i++) {
                            for (int j = 0; j < COLUNAS; j++) {
                                board[i][j] = 0;
                                }    
                            }                        
                        *p = generateRandomPiece(pieces);
                        *fim = 0;
                    } else {
                        *fim = 1;
                    }
                } else {
                    *p = generateRandomPiece(pieces);
                }
            }
        }
    }
}

// Funcao para limpar a tela:
// Impede que a tela fique piscando
// e ajuda na estabilizacao do tempo do jogo
void clear_screen() {
    printf("\033[H\033[J");
}

// Implementacao da funcao game_over
int game_over(int board[LINHAS][COLUNAS]) {
    for (int i = 0; i < 2; i++) { // Verifica apenas as duas primeiras linhas
        for (int j = 0; j < COLUNAS; j++) {
            if (board[i][j] != 0) { // Se ha qualquer celula ocupada nas primeiras linhas
                return 1; // Game over
            }
        }
    }
    return 0; // Jogo continua
}


// Funcao para imprimir o tabuleiro
// Utiliza da funcao de escolher cores de acordo
// com os caracteres identificados e desenha o tabuleiro com as bordas
void print_board(int board[LINHAS][COLUNAS]) {
    printf("\033[1;36m    +------------------------+\033[0m\n"); // Borda superior
    for (int i = 0; i < LINHAS; i++) {
        printf("\033[1;36m    |\033[0m"); // Borda lateral esquerda
        for (int j = 0; j < COLUNAS; j++) {
            printf("%s%d \033[0m", get_piece_color(board[i][j]), board[i][j]);
        }
        printf("\033[1;36m|\033[0m\n"); // Borda lateral direita
        
    }
    printf("\033[1;36m    +------------------------+\033[0m\n"); // Borda inferior
}

void print_title() {

    int titulo[5][16] = {
        {1, 1, 1, 6, 6, 6, 4, 4, 4, 3, 3, 3, 2, 5, 5, 5},
        {0, 1, 0, 6, 0, 0, 0, 4, 0, 3, 0, 3, 2, 5, 0, 0},
        {0, 1, 0, 6, 6, 6, 0, 4, 0, 3, 3, 0, 2, 5, 5, 5},
        {0, 1, 0, 6, 0, 0, 0, 4, 0, 3, 0, 3, 2, 0, 0, 5},
        {0, 1, 0, 6, 6, 6, 0, 4, 0, 3, 0, 3, 2, 5, 5, 5}
    };

    for (int i = 0; i < 5; i++) {
            printf("\033[1;36m|\033[0m");
            for (int j = 0; j < 16; j++) {
                printf("%s%d \033[0m", get_piece_color(titulo[i][j]), titulo[i][j]);
            }
            printf("\033[1;36m|\033[0m\n");
        }
        printf("\n\n");
    }

/*
    Funcao para limpar a peca da matriz
    Conforme a matriz da peca percorre o tabuleiro, os valores das pecas 
    deixam um rastro por onde passam e essa funcao corrige esse
    problema
*/
void clear_piece(int board[LINHAS][COLUNAS], int piece[4][4], int x, int y) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (piece[i][j] != 0 && y + i >= 0 && y + i < LINHAS && x + j >= 0 && x + j < COLUNAS) {
                board[y + i][x + j] = 0; // Limpa a celula correspondente
            }
        }
    }
}

/*
    Funcao para desenhar a peca na matriz
    Redesenha a matriz com os valores da peca a ser colocada
*/
void draw_piece(int board[LINHAS][COLUNAS], int piece[4][4], int x, int y) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (piece[i][j] != 0) { // So desenha as partes da peca (nao as celulas vazias)
                if (y + i >= 0 && y + i < LINHAS && x + j >= 0 && x + j < COLUNAS) {
                    board[y + i][x + j] = piece[i][j]; // Atualiza o tabuleiro com a peca
                }
            }
        }
    }
}

void move_piece(int board[LINHAS][COLUNAS], int piece[4][4], int *x, int *y, char key) {
    // Limpar posicao anterior

        // Calcula nova posicao proposta
    int new_x = *x;
    int new_y = *y;

    if (key == 'a') new_x--;   // Mover para esquerda
    if (key == 'd') new_x++;   // Mover para direita
    if (key == 's') new_y++;   // Mover para baixo
    if (key == 'w') 
        if (can_rotate(board, piece, *x, *y))
    {
        rotate_piece(piece);
    }
    // Verifica colisao na nova posicao proposta
    if (!check_collision(board, piece, new_x, new_y)) {
        // Limpa posicao anterior
        clear_piece(board, piece, *x, *y);

        // Atualiza a posicao
        *x = new_x;
        *y = new_y;

    }
}


// Funcao para inicializar as pecas do Tetris
void initializePieces(int pieces[7][4][4]) {
    // Peca I
    int I[4][4] = {
        {0,0,0,0},
        {6,6,6,6},
        {0,0,0,0},
        {0,0,0,0}
    };

    // Peca O
    int O[4][4] = {
        {0,0,0,0},
        {0,6,6,0},
        {0,6,6,0},
        {0,0,0,0}
    };

    // Peca T
    int T[4][4] = {
        {0,0,0,0},
        {0,5,0,0},
        {5,5,5,0},
        {0,0,0,0}   
    };

    // Peca L
    int L[4][4] = {
        {0,0,0,0},
        {0,0,4,0},
        {4,4,4,0},
        {0,0,0,0}
    };

    // Peca J
    int J[4][4] = {
        {0,0,0,0},
        {3,0,0,0},
        {3,3,3,0},
        {0,0,0,0}
    };

    // Peca S
    int S[4][4] = {
        {0,0,0,0},
        {0,2,2,0},
        {2,2,0,0},
        {0,0,0,0}
    };

    // Peca Z
    int Z[4][4] = {
        {0,0,0,0},
        {1,1,0,0},
        {0,1,1,0},
        {0,0,0,0}
    };


    // Copiar as pecas para o array `pieces`
    memcpy(pieces[0], I, sizeof(I));
    memcpy(pieces[1], O, sizeof(O));
    memcpy(pieces[2], T, sizeof(T));
    memcpy(pieces[3], S, sizeof(S));
    memcpy(pieces[4], Z, sizeof(Z));
    memcpy(pieces[5], J, sizeof(J));
    memcpy(pieces[6], L, sizeof(L));
}





// Funcao para gerar uma peca aleatoria
Piece generateRandomPiece(int pieces[7][4][4]) {
    Piece new_piece;
    int random_index = rand() % 7; // Gera um indice aleatorio entre 0 e 6
    memcpy(new_piece.piece, pieces[random_index], sizeof(new_piece.piece));
    new_piece.posX = COLUNAS / 2 - 2; // Centraliza a peca
    new_piece.posY = 0; // Posicao inicial no topo
    return new_piece;
}



void fix_piece(int board[LINHAS][COLUNAS], int piece[4][4], int x, int y) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (piece[i][j] != 0) {
                board[y + i][x + j] = piece[i][j]; // Transfere a peca para o tabuleiro
            }
        }
    }
}

void rotate_piece(int piece[4][4]) {
    int rotated[4][4] = {0}; // Matriz auxiliar para armazenar a peca rotacionada

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            rotated[j][3 - i] = piece[i][j]; // Rotacao de 90 graus
        }
    }

    // Copiar a peca rotacionada de volta para a matriz original
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            piece[i][j] = rotated[i][j];
        }
    }
}


int check_collision(int board[LINHAS][COLUNAS], int piece[4][4], int x, int y) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (piece[i][j] != 0) { // Apenas verifica celulas ocupadas da peca
                int new_x = x + j;
                int new_y = y + i;

                // Verifica limites do tabuleiro
                if (new_x < 0 || new_x >= COLUNAS || new_y >= LINHAS) {
                    return 1; // Colisao com borda ou fundo
                }

                // Verifica colisao com pecas fixadas
                if (new_y >= 0 && board[new_y][new_x] != 0) {
                    return 1; // Colisao com outra peca
                }
            }
        }
    }
    return 0; // Sem colisoes
}


int can_rotate(int board[LINHAS][COLUNAS], int piece[4][4], int x, int y) {
    int rotated[4][4] = {0};

    // Simula a rotacao
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            rotated[j][3 - i] = piece[i][j];
        }
    }

    // Verifica se a peca rotacionada colide com o tabuleiro
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (rotated[i][j] != 0) {
                int new_x = x + j;
                int new_y = y + i;

                // Fora do tabuleiro
                if (new_x < 0 || new_x >= COLUNAS || new_y >= LINHAS) {
                    return 0;
                }

                // Colisao com peca fixa
                if (board[new_y][new_x] != 0) {
                    return 0;
                }
            }
        }
    }
    return 1; // Pode rotacionar
}

int check_complete_lines(int board[LINHAS][COLUNAS], int *score) {
    int complete_lines = 0;

    for (int i = 0; i < LINHAS; i++) {
        int is_complete = 1;

        for (int j = 1; j < COLUNAS - 1; j++) { // Ignora bordas laterais
            if (board[i][j] == 0) {
                is_complete = 0;
                break;
            }
        }

        if (is_complete) {
            complete_lines++;
            // Remove a linha
            remove_line(board, i);
            *score = *score + 100;
        }
    }

    return complete_lines;
}
void remove_line(int board[LINHAS][COLUNAS], int line) {
    for (int i = line; i > 0; i--) {
        for (int j = 1; j < COLUNAS - 1; j++) { // Ignora bordas laterais
            board[i][j] = board[i - 1][j];
        }
    }

    // Zera a linha superior (apos o deslocamento)
    for (int j = 1; j < COLUNAS - 1; j++) {
        board[0][j] = 0;
    }
}


// Configurar terminal para modo nao-canonico
void enable_raw_mode() {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag &= ~(ICANON | ECHO); // Desabilitar modo canonico e eco
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}