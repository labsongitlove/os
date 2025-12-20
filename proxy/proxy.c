#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>

#define BUFFER_SIZE 8192

// Простейшая структура для хранения HTTP заголовка
typedef struct header_t {
    char name[256];
    char value[1024];
} header_t;

// Парсим заголовки, возвращает количество заголовков
// Возвращает -1 при ошибке (некорректный формат)
int parse_headers(char *buf, header_t *headers, int max_headers, int *headers_end_pos) {
    int count = 0;
    char *line = buf;
    while (1) {
        char *next_line = strstr(line, "\r\n");
        if (!next_line) return -1; // ошибка — заголовок не завершён
        if (line == next_line) { // пустая строка - конец заголовков
            *headers_end_pos = (int)(next_line - buf) + 2;
            break;
        }

        if (count >= max_headers) return -1;

        int len = (int)(next_line - line);
        char *colon = memchr(line, ':', len);
        if (!colon) return -1;

        int name_len = (int)(colon - line);
        int value_len = len - name_len - 1;

        // Копируем имя заголовка
        if (name_len >= (int)sizeof(headers[count].name)) return -1;
        strncpy(headers[count].name, line, name_len);
        headers[count].name[name_len] = '\0';

        // Копируем значение заголовка (пропускаем пробелы)
        const char *val_start = colon + 1;
        while (*val_start == ' ' && value_len > 0) {
            val_start++;
            value_len--;
        }
        if (value_len >= (int)sizeof(headers[count].value)) return -1;
        strncpy(headers[count].value, val_start, value_len);
        headers[count].value[value_len] = '\0';

        count++;
        line = next_line + 2;
    }
    return count;
}

// Поиск заголовка по имени (регистр игнорируем)
const char* find_header(header_t *headers, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcasecmp(headers[i].name, name) == 0) {
            return headers[i].value;
        }
    }
    return NULL;
}

// Резолвинг имени хоста (возвращает 0 при успехе)
int resolve_hostname(const char *hostname, struct sockaddr_in *addr) {
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res;

    if (getaddrinfo(hostname, NULL, &hints, &res) != 0) {
        perror("getaddrinfo");
        return -1;
    }

    if (res == NULL) return -1;

    struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
    addr->sin_family = AF_INET;
    addr->sin_addr = ipv4->sin_addr;

    freeaddrinfo(res);
    return 0;
}

void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    ssize_t received = 0;
    ssize_t total_received = 0;

    // Читаем запрос клиента (HTTP 1.0 не поддерживает chunked, поэтому читаем заголовки)
    while (1) {
        received = read(client_fd, buffer + total_received, BUFFER_SIZE - total_received - 1);
        if (received < 0) {
            perror("read client");
            close(client_fd);
            return NULL;
        }
        if (received == 0) {
            // Клиент закрыл соединение
            close(client_fd);
            return NULL;
        }
        total_received += received;
        buffer[total_received] = 0;

        // Проверим есть ли уже полные заголовки (оканчиваются на \r\n\r\n)
        if (strstr(buffer, "\r\n\r\n")) break;

        if (total_received >= BUFFER_SIZE - 1) {
            fprintf(stderr, "Request headers too large\n");
            close(client_fd);
            return NULL;
        }
    }

    // Разбор первой строки запроса
    char method[16], url[1024], version[16];
    if (sscanf(buffer, "%15s %1023s %15s", method, url, version) != 3) {
        fprintf(stderr, "Invalid request line\n");
        close(client_fd);
        return NULL;
    }
    if (strcmp(method, "GET") != 0) {
        fprintf(stderr, "Only GET method is supported\n");
        close(client_fd);
        return NULL;
    }

    if (strncmp(version, "HTTP/1.", 7) != 0) {
        fprintf(stderr, "Only HTTP/1.x is supported\n");
        close(client_fd);
        return NULL;
    }

    // Парсим заголовки
    header_t headers[32];
    int headers_end_pos = 0;
    int headers_count = parse_headers(buffer + (strchr(buffer, '\n') - buffer) + 1, headers, 32, &headers_end_pos);
    if (headers_count < 0) {
        fprintf(stderr, "Failed to parse headers\n");
        close(client_fd);
        return NULL;
    }

    // Получаем хост из заголовков
    const char *host_header = find_header(headers, headers_count, "Host");
    if (!host_header) {
        fprintf(stderr, "Host header missing\n");
        close(client_fd);
        return NULL;
    }

    // Формируем sockaddr_in для сервера
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(80);

    if (inet_pton(AF_INET, host_header, &server_addr.sin_addr) <= 0) {
        // Если не IP, пытаемся резолвить DNS
        if (resolve_hostname(host_header, &server_addr) != 0) {
            fprintf(stderr, "Could not resolve hostname: %s\n", host_header);
            close(client_fd);
            return NULL;
        }
    }

    // Подключаемся к серверу
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        close(client_fd);
        return NULL;
    }

    if (connect(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(client_fd);
        close(server_fd);
        return NULL;
    }

    // Отправляем запрос серверу полностью
    ssize_t sent = 0;
    ssize_t to_send = total_received;
    while (sent < to_send) {
        ssize_t n = write(server_fd, buffer + sent, to_send - sent);
        if (n <= 0) {
            perror("write to server");
            close(client_fd);
            close(server_fd);
            return NULL;
        }
        sent += n;
    }

    // Читаем ответ сервера и пересылаем клиенту
    while ((received = read(server_fd, buffer, BUFFER_SIZE)) > 0) {
        ssize_t written = 0;
        while (written < received) {
            ssize_t n = write(client_fd, buffer + written, received - written);
            if (n <= 0) {
                perror("write to client");
                close(client_fd);
                close(server_fd);
                return NULL;
            }
            written += n;
        }
    }

    if (received < 0) {
        perror("read from server");
    }

    close(client_fd);
    close(server_fd);

    return NULL;
}

bool check_port(const char *port_str) {
    if (!port_str || !*port_str)
        return false;

    // Проверяем, что все символы — цифры
    for (const char *p = port_str; *p; p++) {
        if (!isdigit((unsigned char)*p))
            return false;
    }

    // Проверяем числовое значение порта
    long port = strtol(port_str, NULL, 10);
    if (port < 1 || port > 65535)
        return false;

    return true;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (!check_port(argv[1])) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, 128) < 0) {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("HTTP/1.0 proxy listening on port %d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            perror("malloc");
            continue;
        }
        *client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd < 0) {
            perror("accept");
            free(client_fd);
            continue;
        }

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&tid, &attr, handle_client, client_fd) != 0) {
            perror("pthread_create");
            close(*client_fd);
            free(client_fd);
        }
        pthread_attr_destroy(&attr);
    }

    close(listen_fd);
    return EXIT_SUCCESS;
}
