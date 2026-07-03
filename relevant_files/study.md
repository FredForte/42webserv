
struct addrinfo

```C
struct addrinfo {
    int              ai_flags;     // AI_PASSIVE, AI_CANONNAME, etc.
    int              ai_family;    // AF_INET, AF_INET6, AF_UNSPEC
    int              ai_socktype;  // SOCK_STREAM, SOCK_DGRAM
    int              ai_protocol;  // use 0 for "any"
    size_t           ai_addrlen;   // size of ai_addr in bytes
    struct sockaddr *ai_addr;      // struct sockaddr_in or _in6
    char            *ai_canonname; // full canonical hostname

    struct addrinfo *ai_next;      // linked list, next node
};
```

Essa struct é utilizada para preparar as estruturas de socket address para uso subsequentes. Também é utilizada achar host name, e serviço look ups.

Carregue essa struct e depois chame getaddrinfo(). Essa função retorna um pontiero para uma nova lista linkada dessas structs preenchidas com o conteúdo que precisamos.

Podemos forçar IPv4 ou IPv6 no campo "ai_next", ou deixar AF_UNSPEC. É bom porque seu código pode ser agnóstico de IP.

O artigo sugere que você use o primeiro resultado da lista linkada para usar, mas que o nosso cenário pode ser diferente.

O campo "ai_addr" dentro da struct "addrinfo" é um ponteiro para a struct "sockaddr".

Na maior parte das vezes, você vai querer usar a função getaddrinfo() para preencher a nossa struct addrinfo, mas ainda assim você vai querer usar os campos internos dessa struct.

A struct sockaddr armazena um endereço de socket para vários tipos de sockets.

---

struct sockaddr

```C
struct sockaddr {
    unsigned short    sa_family;    // address family, AF_xxx
    char              sa_data[14];  // 14 bytes of protocol address
}; 
```

`sa_family` pode ser AF_INET para IPv4 ou AF_INET6 para IPv6.

`sa_data` contém o endereço de destino junto com a porta. Você não vai querer preencher isso na mão.


Para lidar com `struct sockaddr`, programadores criaram uma struct paralela `struct sockaddr_in` para uso com IPv4.

Um ponteiro para `struct sockaddr_in` pode ser casteado para um ponteiro `struct sockaddr` e vice-versa. Então, ainda que a função `connect()` quera um `struct sockaddr*`, você ainda pode usar `struct sockaddr_in`.

---

struct sockaddr_in

// (IPv4 only--see struct sockaddr_in6 for IPv6)

```C
struct sockaddr_in {
    short int          sin_family;  // Address family, AF_INET
    unsigned short int sin_port;    // Port number
    struct in_addr     sin_addr;    // Internet address
    unsigned char      sin_zero[8]; // Same size as struct sockaddr
};
```

`sin_zero` serve para pad a struct para que ela bata com o mesmo tamanho de `struct sockaddr`, e PRECISA ser zerada com `memset`.

sin_family corresponde à `sa_family` de `struct sockaddr`, e nesse caso, já que é IPv4, precisa ser setado para "AF_INET".

`sin_port` precisa está em Network Byte Order (utilizando htons()).

O campo `sin_addr` é uma struct do tipo `struct in_addr`. 


---

// (IPv4 only--see struct in6_addr for IPv6)

```C
// Internet address (a structure for historical reasons)
struct in_addr {
    uint32_t s_addr; // that's a 32-bit int (4 bytes)
};
```

`s_addr` é contém um endereço de IP em Network Byte Order. 

Para IPv6, também temos a mesma ideia.

---

Equivalente IPv6

```C
// (IPv6 only--see struct sockaddr_in and struct in_addr for IPv4)

struct sockaddr_in6 {
    u_int16_t       sin6_family;   // address family, AF_INET6
    u_int16_t       sin6_port;     // port, Network Byte Order
    u_int32_t       sin6_flowinfo; // IPv6 flow information
    struct in6_addr sin6_addr;     // IPv6 address
    u_int32_t       sin6_scope_id; // Scope ID
};

struct in6_addr {
    unsigned char   s6_addr[16];   // IPv6 address
};
```

Assim como IPv4, a versão IPv6 também possui porta.

---

sockaddr_storage

```C
struct sockaddr_storage {
    sa_family_t  ss_family;     // address family

    // all this is padding, implementation specific, ignore it:
    char      __ss_pad1[_SS_PAD1SIZE];
    int64_t   __ss_align;
    char      __ss_pad2[_SS_PAD2SIZE];
};
```

É feita para ser grande o suficiente para armazenar tanto a struct IPv4 quanto a IPv6. Como muitas vezes não sabemos se a nossa struct sockaddr vai ser preenchida com IPv4 ou IPv6, esta pode ser útil.

Depois, checando por `ss_family`, você sabe se é IPv4 ou IPv6, e poderá dar cast para struct sockaddr_in ou struct sockaddr_in6 se necessário.

---

getaddrinfo

```C
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int getaddrinfo(const char *node,   // e.g. "www.example.com" or IP
                const char *service,  // e.g. "http" or port number
                const struct addrinfo *hints,
                struct addrinfo **res);
```

node é endereço ou ip.

service é tipo de porta ou serviço, que pode ser "http", "ftp", "telnet", "smtp".

Só setando tudo para que funcione, mas ainda não fazendo nada útil:

```C
int status;
struct addrinfo hints;
struct addrinfo *servinfo;  // will point to the results

memset(&hints, 0, sizeof hints); // make sure the struct is empty
hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

if ((status = getaddrinfo(NULL, "3490", &hints, &servinfo)) != 0) {
    fprintf(stderr, "gai error: %s\n", gai_strerror(status));
    exit(1);
}

// servinfo now points to a linked list of 1 or more
// struct addrinfos

// ... do everything until you don't need servinfo anymore ....

freeaddrinfo(servinfo); // free the linked-list
```

Por setar "AI_PASSIVE", isso diz que getaddrinfo() atribuir o enderećo do meu host local nas structs de socket, aí você não precisa setar isso na mão.

Se getaddrinfo() der erro, ela retorna um valor diferente de zero. Pra ver o erro, usamos `gai_strerror()`.

`servinfo` vai apontar para uma lista linkada de struct addrinfo, e cada um contém struct sockaddr, que podemos usar depois.

Sempre precisamos liberar as coisas com `freeaddrinfo()`.

Exemplo que conecta:

```C
int status;
struct addrinfo hints;
struct addrinfo *servinfo;  // will point to the results

memset(&hints, 0, sizeof hints); // make sure the struct is empty
hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
hints.ai_socktype = SOCK_STREAM; // TCP stream sockets

// get ready to connect
status = getaddrinfo("www.example.net", "3490", &hints, &servinfo);

// servinfo now points to a linked list of 1 or more
// struct addrinfos

// etc.
```

---

socket()

```C
#include <sys/types.h>
#include <sys/socket.h>

int socket(int domain, int type, int protocol); 
```

Se você for hardcodar isso na mão, você pode setar algo como:

`domain` é PF_INET or PF_INET6
`type`: SOCK_STREAM or SOCK_DGRAM,
`protocol` pode ser 0 pra escolher o protocolo para o tipo dado. Ou você pode chamar getprotobyname()pra encontrolar o protocolo que você quer, como: “tcp” or “udp”.


AF vem de address family. De "AF_INET". Achavam que isso poderia aceitar todo tipo de protocolo, o que seria o "PF" em "PF_INET". Mas não aconteceu. A coisa certa a se fazer é usar AF_INET na sua struct sockaddr_in e PF_INET na chamada para socket().

Você vai querer usar getaddrinfo() para preencher dados e usar isso na chamada de socket(). Exemplo:

```C
int s;
struct addrinfo hints, *res;

// do the lookup
// [pretend we already filled out the "hints" struct]
getaddrinfo("www.example.com", "http", &hints, &res);

// again, you should do error-checking on getaddrinfo(), and walk
// the "res" linked list looking for valid entries instead of just
// assuming the first one is good (like many of these examples do).
// See the section on client/server for real examples.

s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
```

socket() retorna um socket descriptor que você pode usar em chamadas posteriores, ou então -1 em erro. errno tem o valor de erro.

---

```C
#include <sys/types.h>
#include <sys/socket.h>

int bind(int sockfd, struct sockaddr *my_addr, int addrlen);
```

Depois de ter o socket, nós temos o bind(). Muito utilizado se você for utilizar a função listen() para conexões que irão vir em uma porta específica. 

- sockfd é o socket fd retornado por socket().
- my_addr é um ponteiro para uma struct sockaddr que contém informação do seu endereço (porta e IP).
- addrlen é o tamanho em bytes do endereço.


```C

struct addrinfo hints, *res;
int sockfd;

// first, load up address structs with getaddrinfo():

memset(&hints, 0, sizeof hints);
hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
hints.ai_socktype = SOCK_STREAM;
hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

getaddrinfo(NULL, "3490", &hints, &res);

// make a socket:

sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

// bind it to the port we passed in to getaddrinfo():

bind(sockfd, res->ai_addr, res->ai_addrlen);
```

