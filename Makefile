NAME = webserv
CC = c++
CFLAGS = -Wall -Wextra -Werror -std=c++98

DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CFLAGS += -g
endif

INCLUDES = -I.

SRCS = main.cpp program_flow_utils.cpp  socket_utils.cpp cgi.cpp \
	/utils/utils_config_file.cpp /parser/ConfigParser.cpp /parser/HttpRequestParser.cpp \
	main_functions.cpp /utils/main_functions_utils.cpp	/parser/Tokenizer.cpp \
	/response/HttpResponseCodesIndex.cpp /response/response_handlers.cpp \
	/parser/ConfigValidator.cpp


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
	@rm -rf $(OBJS_PATH)

fclean: clean
	@rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re debug parser_test request_parser_test
