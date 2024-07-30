NAME1 = triServer
NAME2 = triClient
CFLAGS = -Wall 
INCLUDES = -I./inc
SRC_DIR = src
OBJ_DIR = obj

SRCS1 = $(SRC_DIR)/triServer.c $(SRC_DIR)/errExit.c
SRCS2 = $(SRC_DIR)/triClient.c $(SRC_DIR)/errExit.c

OBJS1 = $(SRCS1:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
OBJS2 = $(SRCS2:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

all: $(NAME1) $(NAME2)

$(NAME1): $(OBJS1)
	@echo "Making executable 1: $@"
	@$(CC) $^ -o $@

$(NAME2): $(OBJS2)
	@echo "Making executable 2: $@"
	@$(CC) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "Compiling: $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

.PHONY: clean

clean:
	@rm -f $(OBJ_DIR)/*.o $(NAME1) $(NAME2)
	@echo "Removed object files and executables..."
