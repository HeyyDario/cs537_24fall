# CS537 P1: Solution to Letter Boxed
## Author Info:
* **Name**: Chengtao Dai

* **CS Login**: chengtao

* **Wisc ID**: 90852465877

* **Email**: cdai53@wisc.edu

## How to Run the Program:
1. Compile the program using gcc: `gcc -g -o letter-boxed letter-boxed.c`

2. Run the program by passing the board file and dictionary file as command-line arguments: `./letter-boxed <board_file> <dictionary_file>`

3. Input the solution through standard input.

## Implementation logistics:
### Global variable
`char** board`: A 2D array (pointer to pointer) that holds the letters on each side of the board. Each board[i] represents a side, and board[i][j] represents a letter on that side.

`char** dict`: A dynamically allocated array of strings, where each string is a word from the dictionary.

`int num_sides`: Tracks the number of sides on the board.

`int dict_size`: Keeps track of the number of words in the dictionary.

`int dict_capacity`: Stores the current capacity of the dictionary array.

`char** solution`: Stores the user's proposed solution, entered through standard input.

`int solution_size`: Tracks the number of words in the solution array.

`int letters_counter_board[NUM_LET]`: Tracks the number of times each letter on the board is used (each letter corresponds to an index).

`int letters_counter_solution[NUM_LET]`: Tracks the occurrence of letters used in the solution.

### Logic in main

1. File Reading: reads board and dictionary into `char** board` and `char** dict`.

2. Solution Testing: reads the user’s input (the solution) and verifies whether it meets all the rules of the game.

3. Memory Cleaning: deallocates memory that was dynamically allocated for the board, dictionary, and solution arrays.

### Logic of Various Functions

`void read_board(const char* fileName)`: Reads the board configuration from the file and loads it into the global `board` array. After loading the board, checks if the number of sides is valid (at least 3), and verifies that no letter appears more than once across different sides. If an invalid board is detected, the program prints an error and exits.

`void read_dict(const char* fileName)`: Reads words from the dictionary file and stores them in the `dict` array.

`void read_solution()`: Reads the user's solution from `stdin`, stores it in the `solution` array, and updates the letter usage count in `letters_counter_solution`.

`int is_in_dict(const char* word)`: Verifies if a word in the solution exists in the dictionary.

`int prev_match_curr(const char* prev, const char* curr)`: Checks whether the last character of the previous word matches the first character of the current word.

`int is_same_side_consecutive(const char* word)`: Ensures no two consecutive letters in a word are from the same side of the board.

`int is_on_board(const char* word)`: Ensures all letters of a word exist on the board.

`void test_solution()`: Tests the solution against all the rules.