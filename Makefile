CC = gcc
CFLAGS = -Wall -Wextra -g
LDLIBS = -lssh	
SRC_DIR = src
OBJ_DIR = obj
BIN = my_htop

# Liste des fichiers sources
SRCS = $(wildcard $(SRC_DIR)/*.c)
# Transformation .c -> .o
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Cible par défaut
all: $(BIN)

# Création de l'exécutable
$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Compilation des objets
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

# Création du dossier obj s'il n'existe pas
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Nettoyage
clean:
	rm -rf $(OBJ_DIR) $(BIN)

.PHONY: all clean


