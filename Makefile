NAME = webserv
CC = c++
CFLAGS = -Wall -Wextra -Werror -std=c++98

DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CFLAGS += -g
endif

INCLUDES = -I.

SRCS = main.cpp

PARSER_TEST_NAME = parser_test
PARSER_TEST_SRCS = parser/main_parser.cpp parser/Tokenizer.cpp parser/ConfigParser.cpp

REQUEST_PARSER_TEST_NAME = request_parser_test
REQUEST_PARSER_TEST_SRCS = parser/main_request_parser.cpp parser/HttpRequestParser.cpp

CONFIG_REQUEST_TEST_NAME = config_request_test
CONFIG_REQUEST_TEST_SRCS = parser/main_parser_request.cpp parser/HttpRequestParser.cpp parser/Tokenizer.cpp parser/ConfigParser.cpp response/HttpResponseCodesIndex.cpp

SRCS_PATH = src
OBJS_PATH = objs
OBJS = $(SRCS:%.cpp=$(OBJS_PATH)/%.o)
PARSER_TEST_OBJS = $(PARSER_TEST_SRCS:%.cpp=$(OBJS_PATH)/%.o)
REQUEST_PARSER_TEST_OBJS = $(REQUEST_PARSER_TEST_SRCS:%.cpp=$(OBJS_PATH)/%.o)
CONFIG_REQUEST_TEST_OBJS = $(CONFIG_REQUEST_TEST_SRCS:%.cpp=$(OBJS_PATH)/%.o)

all: $(NAME)

debug: fclean
	@$(MAKE) DEBUG=1

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(NAME)

parser_test: $(PARSER_TEST_OBJS)
	$(CC) $(CFLAGS) $(PARSER_TEST_OBJS) -o $(PARSER_TEST_NAME)

request_parser_test: $(REQUEST_PARSER_TEST_OBJS)
	$(CC) $(CFLAGS) $(REQUEST_PARSER_TEST_OBJS) -o $(REQUEST_PARSER_TEST_NAME)

config_request_test: $(CONFIG_REQUEST_TEST_OBJS)
	$(CC) $(CFLAGS) $(CONFIG_REQUEST_TEST_OBJS) -o $(CONFIG_REQUEST_TEST_NAME)

$(OBJS_PATH)/%.o: $(SRCS_PATH)/%.cpp
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@rm -rf $(OBJS_PATH)

fclean: clean
	@rm -f $(NAME) $(PARSER_TEST_NAME) $(REQUEST_PARSER_TEST_NAME)

re: fclean all

.PHONY: all clean fclean re debug parser_test request_parser_test
