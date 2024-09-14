/* ************************************************************************
> File Name:     letter-boxed.c
> Author:        Chengtao Dai
> cs login:         chengtao
> Created Time:  Tue  9/10 21:21:19 2024
> Description:  See README.md

> COMMIT LOG:
Date:9/10/2024          Commit Message:"Defined function prototypes and set up
										basic log in main()."
Date:9/11/2024          Commit Message:"Implemented all read functions but not rule check functions." 
Date:9/12/2024          Commit Message:"Implemented rule check functions but not passed test 2." 
Date:9/13/2024          Commit Message:"Passed all tests." 
 ************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOARD_SIZE 5 // seems to be at most 4, but for flexibility we take 5
#define DICT_CAPACITY 1000
#define NUM_LET 26
#define SOLUTION_CAPACITY 1000

// Rules:
// 1. use each letter in the board at least ONCE.
// 2. prev last char = next first char.
// 3. same-side letter NOT use consecutively
// 4. NOT use letters beyond the board.
// 5. only one occurrence of a letter on the board.

// global variable
char** board; // we use ptr of ptr (a 2d array) to load the board.txt;
// board[i] represent which side of the board (at most 4), board[i][j] represents the letter
int num_sides;
char** dict;
int dict_size;
int dict_capacity = DICT_CAPACITY;
int letters_counter_board[NUM_LET] = {0};
char** solution;
int solution_size;
int solution_capacity = SOLUTION_CAPACITY;
int letters_counter_solution[NUM_LET] = {0};

// define function prototypes
void read_board(const char* fileName);
void read_dict(const char* fileName);
void read_solution();
int is_in_dict(const char* word);
int prev_match_curr(const char *prev, const char *curr);
int is_same_side_consecutive(const char *word);
int is_on_board(const char *word);
void test_solution();


int main(int argc, char* argv[]){
	// initial check for the correct number of args
    if(argc != 3){
        printf("Usage: %s <board_file_name> <dict_file_name>\n", argv[0]);
        return 1;
    }

	// read the board and dict file
	read_board(argv[1]);
	read_dict(argv[2]);

	test_solution();

	for(int i = 0; i < num_sides; i++){
		free(board[i]);
	}
	free(board);

	for(int i = 0; i < dict_size; i++){
		free(dict[i]);
	}
	free(dict);

	for(int i = 0; i < solution_size; i++){
		free(solution[i]);
	}
	free(solution);

    return 0;
}

void read_board(const char* fileName){
	// init, NEED TO free later
	num_sides = 0;
	board = malloc(sizeof(char*) * BOARD_SIZE);
	if(!board){
		printf("Failed to allocate memory for board\n");
		exit(1);
	}

	// read file
	FILE *file = fopen(fileName,"r");
	if(!file){
		printf("open board failed\n");
		exit(1);
	}
	char *line = NULL;
	size_t len = 0;
	ssize_t nread;

	while ((nread = getline(&line, &len, file)) != -1){
		line[strcspn(line, "\n")] = 0;
		
		size_t line_length = strlen(line) + 1;

		board[num_sides] = malloc(sizeof(char) * line_length);//or just line_length?
		if(!board[num_sides]){
			printf("Failed to allocate memory for letters.\n");
			exit(1);
		}

		strncpy(board[num_sides++], line, line_length);
	}

	fclose(file);
	free(line);

	// check the validness of board	
	if (num_sides < 3) {
        printf("Invalid board\n");
        exit(1);
    }

    for (int i = 0; i < num_sides; i++) {
        for (int j = 0; board[i][j] != '\0'; j++) {
            char letter = board[i][j];
            if (letter >= 'a' && letter <= 'z') {
                int index = letter - 'a';
                letters_counter_board[index]++;
                if (letters_counter_board[index] > 1) {
                    printf("Invalid board\n");  // Letter appears more than once across sides
                    exit(1);
                }
            } else {
                printf("Invalid board\n");  // If there is a non-alphabet character
                exit(1);
            }
        }
    }

}

void read_dict(const char* fileName){
	// init
	dict_size = 0;
	dict = malloc(sizeof(char*) * DICT_CAPACITY);
	if(!dict){
		printf("Failed to allocate memory for dict.\n");
		exit(1);
	}

	FILE *file = fopen(fileName, "r");
	if(!file){
		printf("open dictionary failed\n");
		exit(1);
	}

	char *line = NULL;
	size_t len = 0;
	ssize_t nread;

	while((nread = getline(&line,&len, file)) != -1){
		line[strcspn(line, "\n")] = 0;

		if(dict_size >= dict_capacity){
			dict_capacity *= 2;
			dict = realloc(dict, sizeof(char *) * dict_capacity);
			if(!dict){
				printf("Failed to reallocate memory to dictionary.\n");
				exit(1);
			}
		}
		size_t line_length = strlen(line) + 1;

		dict[dict_size] = malloc(line_length);//or just line_length?
		if(!dict[dict_size]){
			printf("Failed to allocate memory for words in dictionary.\n");
			exit(1);
		}

		strncpy(dict[dict_size++], line, line_length);
	}

	fclose(file);
	free(line);
}

void read_solution(){
	solution_size = 0;
	solution = malloc(sizeof(char *) * solution_capacity);
	if(!solution){
		printf("Failed to allocate memory for solutions.\n");
		exit(1);
	}

	char *line = NULL;
	size_t len = 0;
	ssize_t nread;

	// read solution
	while((nread = getline(&line, &len, stdin)) != -1){
		line[strcspn(line, "\n")] = 0;

		// check if we need to resize solution
		if(solution_size >= solution_capacity){
			solution_capacity *= 2;
			solution = realloc(solution, sizeof(char *) * solution_capacity);
			if(!solution){
				printf("Failed to resize solution.\n");
				free(line);
				exit(1);
			}
		}

		size_t word_length = strlen(line) + 1;
		solution[solution_size] = malloc(word_length);
		if(!solution[solution_size]){
			printf("Failed to allocate memory for solution.\n");
			free(line);
			exit(1);
		}

		strncpy(solution[solution_size++], line, word_length);
	}

	free(line);

	// Count letters in the solution after loading
    for (int i = 0; i < solution_size; i++) {
        for (size_t j = 0; j < strlen(solution[i]); j++) {
            char letter = solution[i][j];
            if (letter >= 'a' && letter <= 'z') {
                letters_counter_solution[letter - 'a']++;
            }
        }
    }
}

int is_in_dict(const char *word){
	for(int i = 0; i < dict_size; i++){
		if(strcmp(dict[i], word) == 0){return 1;}
	}
	return 0;// not found
}

int prev_match_curr(const char* prev, const char* curr){
	char last_let_prev = prev[strlen(prev) - 1];
	char first_let_curr = curr[0];

	return last_let_prev == first_let_curr;
}

int is_same_side_consecutive(const char *word){
	int prev_side = -1;

	for(int i = 0; word[i] != '\0'; i++){
		char letter = word[i];
		int curr_side = -1;

		for (int j = 0; j < num_sides; j++){
			if(strchr(board[j], letter) != NULL){
				curr_side = j;
				break;
			}
		}
		
		if (curr_side == prev_side){return 1;}
		prev_side = curr_side;
	}

	return 0;
}

int is_on_board(const char *word){
	for (int i = 0; word[i] != '\0'; i++){
		char let = word[i];
		int found = 0;
		
		for(int j = 0; j < num_sides; j++){
			if(strchr(board[j], let) != NULL){
				found = 1;
				break;
			}
		}

		if(!found){
			return 0;
		}
	}
	return 1; //found
}


void test_solution(){
	read_solution();

	// rules check
	// 1. Check that all letters are on the board
	for(int i = 0; i < solution_size; i++){
		if(!is_on_board(solution[i])){
			printf("Used a letter not present on the board\n");
			exit(0);
		}
	}
	// 2. Check that each word is in the dictionary
	for(int i = 0; i < solution_size; i++){
		if(!is_in_dict(solution[i])){
			printf("Word not found in dictionary\n");
			exit(0);
		}
	}
	// 3. Check that consecutive letters within each word are not on same side
	for (int i = 0; i < solution_size; i++){
        if (is_same_side_consecutive(solution[i])){
            printf("Same-side letter used consecutively\n");
            exit(0);
        }
    }

	// 4. Check that last char of prev matches first char of next
	for (int i = 1; i < solution_size; i++){  // Start at the second word
        if (!prev_match_curr(solution[i - 1], solution[i])){
            printf("First letter of word does not match last letter of previous word\n");
            exit(0);
        }
    }

	// 5. Check all letters on the board are used.
	for (int i = 0; i < NUM_LET; i++) {
        if (letters_counter_board[i] > 0 && letters_counter_solution[i] == 0) {
            printf("Not all letters used\n");
            exit(0);
        }
    }

	printf("Correct\n");
	exit(0);
}
