#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#include "run_opts.h"
#include "html_parser.h"

#define MAX_RQ_LENGTH 2048
#define RESPONSE_BUF_LENGTH 2048

//Заголовок пакета, например: Location: http://microsoft.com
typedef struct _header {
	char *name;
	char *value;
	struct _header *next;
} header;

//Описание запроса
typedef struct _request {
	char *hostname;
	char *uri;
	header *headers;
} request;

typedef int (*html_parser_fcn)(void*, char d);

//Описание ответа
typedef struct _response {
	int code;
	header *headers;
	html_parser_fcn html_parser; //Функция разбора тела сообщения, в данном случае - html.
	void* html_parser_userdata;  //Пользовательские данные для функции
} response;

struct _listener;
typedef int (*listen_fcn)(struct _listener *, response *, const char *, int len);

//Представление url в виде хоста и пути к документу на хосте
typedef struct _url_info {
	char *url;
	char *hostname;
	char *uri;
} url_info;


//Возможные состояния парсера протокола
#define LST_STATE_NONE 0
#define LST_STATE_FIRST_LINE 1
#define LST_STATE_HEADERS 2
#define LST_STATE_BODY 3
#define LST_STATE_READY 4

//Возможные состояния парсера переносов строк
#define CRLF_STATE_NONE 0
#define CRLF_STATE_CR   1
#define CRLF_STATE_LF   2

//Результаты разбора одной строки
#define PARSE_ERROR 0
#define PARSE_OK 1

//Парсер протокола
typedef struct _listener {
	listen_fcn listen_callback;
	int state;       //Текущее состояние парсера (парсинг первой строки, парсинг заголовоков, парсинг тела сообщения и т.д.)
	int crlf_state;  //Текущее состояние парсинга строк
	char local_buf[RESPONSE_BUF_LENGTH]; //Временный буфер для хранения текущей строки
	int len;                             //Размер данных  в буфере
} listener;

//Описание подключения к серверу
typedef struct _connection {
	char *hostname;
	char *uri;
	unsigned short port;

	int socket;
	struct sockaddr_in srv_addr;
	listener *lst;    //Парсер, вызываемый при получении данных
} connection;

int listen_callback(listener *lst, response* rsp, const char * data, int len);
void listener_init(listener *lst, listen_fcn listen_callback);
void request_add_header(request *rq, const char* name, const char* value);

//Преобразование url в пару "хост-путьнахосте"
void url_init(url_info *url, const char *c_url) {
	if (0==strncmp("http://", c_url, 7)) { //Избавляемся от протокола
		c_url += 7;
	}
	char* host_name_end = strstr(c_url, "/");
	if (NULL !=host_name_end) { //Пользователь указал URL с слешом и, возможно, URI на сервере
		int host_name_len = host_name_end - c_url;
		url->hostname = (char*)malloc(host_name_len+1);
		strncpy(url->hostname, c_url, host_name_len);
		url->hostname[host_name_len] = 0;

		char *uri_start = host_name_end;
		if (strlen(uri_start)) {
			url->uri = (char*)malloc(strlen(uri_start+1));
			strcpy(url->uri, uri_start);
		} else {
			url->uri = (char*)malloc(2);
			strcpy(url->uri, "/");
		}
	} else {
		url->hostname = (char*)malloc(strlen(c_url)+1);
		strcpy(url->hostname, c_url);
		url->uri = (char*)malloc(2);
		strcpy(url->uri, "/");
	}
}

int connection_init(connection *con, url_info *url, unsigned short port) {
	memset(con, 0, sizeof(connection));
	con->hostname = url->hostname;
	con->port = port;
	con->lst = (listener*)malloc(sizeof(listener));
	listener_init(con->lst, listen_callback);
	return 0;
}

int connection_set_listener(connection *con, listener* lst) {
	con->lst = lst;
	return 0;
}

int connection_open(connection *con) {
	con->socket = socket(AF_INET, SOCK_STREAM, 0);
	if(con->socket < 0) {
		perror("socket");
		exit(-1);
	}

	con->srv_addr.sin_family = AF_INET;
	con->srv_addr.sin_port = htons(con->port);

	if (! inet_aton(con->hostname, &con->srv_addr.sin_addr)) { //Пытаемся интерпретировать hostname как ip адрес
		struct hostent *host_info = gethostbyname(con->hostname);
		if(NULL == host_info) {
			fprintf(stderr, "Couldnt resolv host %s\r\n", con->hostname);
			close(con->socket);
			exit(-1);
		}
		memcpy(&con->srv_addr.sin_addr, host_info->h_addr, sizeof(con->srv_addr.sin_addr));
	}

	int rc = connect(con->socket, (struct sockaddr *)&con->srv_addr, sizeof(con->srv_addr));
	if (rc < 0) {
		perror("connect");
		close(con->socket);
		exit(-1);
	}
	return 0;
}
int connection_close(connection *con) {
	close(con->socket);
}

int connection_send_request(connection *con, const char *c_rq) {
	int rc = write(con->socket, c_rq, strlen(c_rq));
	if (rc < 0) {
		perror("send request");
	}
	return rc;
}

int connection_listen(connection *con, response *rsp) {
	struct pollfd pfd;
	char buf[256000];
	pfd.fd = con->socket;
	pfd.events = POLLIN | POLLHUP;
	pfd.revents = 0;
	int retries = 0;
	int pol_res = 0;
	while (retries < 20) {
		pfd.revents = 0;
		while ((pol_res=poll(&pfd, 1, 500)) >=0 && retries<20) {
			retries += 1;
			if (pfd.revents & POLLIN) {
				int rc = recv(con->socket, buf, 256000, 0);
				if (0==rc) return 0;
				retries = 0;
				buf[rc] = 0;
				con->lst->listen_callback(con->lst, rsp, buf, rc);
			}
			if (pfd.revents & POLLHUP) {
				break;
			}
			pfd.revents = 0;
		}
	}
	if (retries>=20) {
		close(con->socket);
		printf("Server stopped to send data, exiting...\r\n");
		exit(1);
	}
	return 0;
}

void listener_init(listener *lst, listen_fcn listen_callback) {
	memset(lst, 0, sizeof(listener));
	lst->listen_callback = listen_callback;
}

header* header_create(const char* name, const char* value) {
	header *hdr = (header*)malloc(sizeof(header));
	memset(hdr, 0, sizeof(header));
	hdr->name = (char*)strdup(name);
	hdr->value = (char*)strdup(value);
	return hdr;
}

void request_init(request *rq, url_info *url) {
	memset(rq, 0, sizeof(request));
	rq->hostname = url->hostname;
	rq->uri = url->uri;
	request_add_header(rq, "Host", url->hostname);
}

void request_add_header(request *rq, const char* name, const char* value) {
	if (NULL == rq->headers) {
		rq->headers = header_create(name, value);
		return;
	}
	header *cur_header = rq->headers;
	while (cur_header->next) {
		cur_header = cur_header->next;
	}
	cur_header->next = header_create(name, value);
}

char* request_build(request *rq) {
	char *c_rq = (char*)malloc(MAX_RQ_LENGTH);
	c_rq[0] = 0;
	//Формируем стартовую строку
	strcpy(c_rq, "GET ");
	strcat(c_rq, rq->uri);
	strcat(c_rq, " HTTP/1.1\r\n");

	//Формируем заголовки
	header *cur_hdr = rq->headers;
	while (cur_hdr) {
		strcat(c_rq, cur_hdr->name);
		strcat(c_rq, ": ");
		strcat(c_rq, cur_hdr->value);
		strcat(c_rq, "\r\n");
		cur_hdr = cur_hdr->next;
	}

	//Формируем пустую строку
	strcat(c_rq, "\r\n");

	//printf("%s\r\n", c_rq);
	return c_rq;
}

void response_init(response *rsp) {
	memset(rsp, 0, sizeof(request));
}

void response_add_header(response *rsp, const char* name, const char* value) {
	if (NULL == rsp->headers) {
		rsp->headers = header_create(name, value);
		return;
	}
	header *cur_header = rsp->headers;
	while (cur_header->next) {
		cur_header = cur_header->next;
	}
	cur_header->next = header_create(name, value);
}

void response_set_html_parser(response *rsp, html_parser_fcn fcn, void* userdata) {
	rsp->html_parser = fcn;
	rsp->html_parser_userdata = userdata;
}

int is_line_complete(listener *lst, char data) {
	if ('\r'==data) {
		lst->crlf_state = CRLF_STATE_CR;
	} else if ('\n'==data && CRLF_STATE_CR==lst->crlf_state) {
		lst->crlf_state = CRLF_STATE_LF;
	} else {
		lst->crlf_state = CRLF_STATE_NONE;
	}
	if (CRLF_STATE_LF == lst->crlf_state)
		return 1;
	return 0;
}

//Парсинг первой строки ответа сервера
int parse_first_line(listener *lst, response *rsp) {
	char *code_ptr = strstr(lst->local_buf, " ");
	if (NULL == code_ptr || 0==*(code_ptr+1)) {
		return PARSE_ERROR;
	}
	rsp->code = strtol(code_ptr+1, NULL, 0);
	lst->len = 0;
	lst->local_buf[0] = 0;
	return PARSE_OK;
}

//Парсинг строки с заголовком
int parse_header(listener *lst, response *rsp) {
	char *semicolon = strstr(lst->local_buf, ":");
	if (NULL == semicolon) {
		return PARSE_ERROR;
	}
	char param_name[1024]; //FIXME - Опасно! Обеспечить динамическое определение размера
	char param_value[4096];
	strncpy(param_name, lst->local_buf, (semicolon-lst->local_buf));
	param_name[(semicolon-lst->local_buf)] = 0;
	strncpy(param_value, semicolon+2, strlen(semicolon+2)-2);
	param_value[strlen(semicolon+2)-2] = 0;
	response_add_header(rsp, param_name, param_value);
	lst->len = 0;
	lst->local_buf[0] = 0;
	return 0;
}

int listen_callback(listener *lst, response* rsp, const char * data, int len) {
	int i = 0;
	for (i=0;i<len;i++) {
		int line_complete = 0;
		char d = data[i];
		if (lst->state!=LST_STATE_BODY && lst->len<RESPONSE_BUF_LENGTH-2) {
			lst->local_buf[lst->len] = d;
			lst->len++;
			lst->local_buf[lst->len] = 0;
			line_complete = is_line_complete(lst, d);
		}
		switch (lst->state) {
			case LST_STATE_NONE:
				lst->state = LST_STATE_FIRST_LINE;
				break;
			case LST_STATE_FIRST_LINE:
				if (line_complete) {
					parse_first_line(lst, rsp);
					lst->state = LST_STATE_HEADERS;
				}
				break;
			case LST_STATE_HEADERS:
				if (line_complete) {
					if (lst->len>2) {
						parse_header(lst, rsp);
						lst->state = LST_STATE_HEADERS;
					} else {
						lst->state = LST_STATE_BODY;
					}
				}
				break;
			case LST_STATE_BODY: //Никакого парсинга - только сохранение данных
				if (rsp->html_parser) {
					rsp->html_parser(rsp->html_parser_userdata, d);
				}
					//printf("%c", d);
					break;
			default:
				break;
		}
	}
	return 0;
}

char *get_redirect_url(response *rsp) {
	header *cur_hdr = rsp->headers;
	while (NULL != cur_hdr) {
		if (0==strcmp("Location", cur_hdr->name)) {
			return cur_hdr->value;
		}
		cur_hdr = cur_hdr->next;
	}
	return NULL;
}

int main(int argc, const char *argv[]) {
	if (argc<2 || run_options_parse(argc, argv) || run_opts.show_help) {
		show_help(argv[0]);
		exit(1);
	}
	while (1) {
		connection con;     //Информация о текущем соединении с сервером
		request rq;         //HTTP-запрос к серверу
		response rsp;       //Ответ от сервера
		url_info url;       //URL, с которого необходимо получить данные
		html_parser hp;     //Парсер, получающий содержимое веб-страницы

		url_init(&url, run_opts.url);                //Разбиваем URL на имя хоста и путь к документу на сервере
		connection_init(&con, &url, run_opts.port);  //Готовим соединение с сервером на основании имени хоста
		request_init(&rq, &url);                     //Готовим прототип запроса
		response_init(&rsp);                         //Готовим прототип ответа сервера
		html_parser_init(&hp, run_opts.file_name);   //Готовим парсер веб страницы
		response_set_html_parser(&rsp, (html_parser_fcn)html_parse, (void*)&hp); //И устанавливаем его для разбора приниамемой страницы

		//Добавляем заголовки. Можно добавить куки их ответа
		request_add_header(&rq, "User-Agent", "Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9b5) Gecko/2008050509 Firefox/3.0b5");
		request_add_header(&rq, "Accept",  "text/html");
		request_add_header(&rq, "Connection", "close");

		char *c_rq = request_build(&rq);                 //Преобразуем запрос в тестовую строку

		printf("Connecting...\r\n");
		connection_open(&con);                           //Подключаемся к серверу
		printf("Sending request...\r\n");
		connection_send_request(&con, c_rq);             //Посылаем запрос
		printf("Waiting for server to send data...\r\n");
		connection_listen(&con, &rsp);                   //Слушаем ответ
		connection_close(&con);

		html_parser_close(&hp);

		printf("Completed with code %d ", rsp.code);
		if (rsp.code>=100 && rsp.code<200) {
			printf("(INFO)\r\n");
		} else if (rsp.code>=200 && rsp.code<300) {
			printf("(SUCCESS)\r\n");
		} else if (rsp.code>=300 && rsp.code<400) {
			printf("(REDIRECT)\r\n");
			char *redirect_url = get_redirect_url(&rsp);
			if (NULL != redirect_url) {
				printf("Redirected to: %s\r\n", redirect_url);
				if (run_opts.allow_redirect) {
					run_opts.url = redirect_url;
					continue;
				}
			}
		} else if (rsp.code>=400 && rsp.code<500) {
			printf("(CLIENT ERROR)\r\n");
		} else if (rsp.code>=500) {
			printf("(SERVER ERROR)\r\n");
		}
		break;
	}
	return 0;
}
