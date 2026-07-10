NAME = webserv
CC = c++
CFLAGS = -Wall -Wextra -Werror -std=c++98

DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CFLAGS += -g
endif

INCLUDES = -I.

SRCS = main.cpp program_flow_utils.cpp

SRCS_PATH = src
OBJS_PATH = objs
OBJS = $(SRCS:%.cpp=$(OBJS_PATH)/%.o)

all: $(NAME)

debug: fclean
	@$(MAKE) DEBUG=1

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(NAME)

$(OBJS_PATH)/%.o: $(SRCS_PATH)/%.cpp
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@rm -rf $(OBJS)

fclean: clean
	@rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re debug
