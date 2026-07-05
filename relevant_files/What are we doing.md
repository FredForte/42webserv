## How a server works

A server needs to handle multiple connections at once. When a client connects, the server should remain available for others — it can't stop everything to serve just one.

There are different ways to solve this. The most intuitive is thread-per-connection: each client gets its own thread with a linear flow of reading, processing, and responding. Easy to reason about, but tends to scale poorly — many threads mean memory overhead and constant context switching.

The alternative is the event-driven model: a single thread monitors multiple fds at once and only reacts when there's activity. More resource-efficient, but the complexity that would be hidden inside threads is now in your hands — you manage each connection's state manually. This is the model we'll use.

What follows describes how it works in practice: the main loop, epoll's role, and how partial reads and writes are handled without blocking the server.

---

The flow looks something like this: create an endpoint (`socket()`), give it an address (`bind()`), put it in listening mode (`listen()`), and register that fd in epoll (`epoll_ctl()`).

From there it can be a single loop. `epoll_wait()` blocks until some fd has activity. When it returns, it can be one of two things: the listening socket (new connection — call `accept()` and register the client fd in epoll), or an already-connected client fd (data to read or space to write).

All fds are set to non-blocking (`fcntl()`). In this mode, `read()` and `write()` do what they can and return immediately. A `read()` may bring only part of an HTTP request because TCP is a byte stream — the buffer may hold only part of the data at that moment. A `write()` of 10MB may only manage 64KB because the kernel's send buffer is finite. In both cases, the return value tells you how many bytes were processed. If the buffer is full, it returns `EAGAIN` — "not now, try later".

That's why each connection needs a structure tracking its state: how much was read, how much was written, where it left off. When `write()` can't send everything, you register `EPOLLOUT` on that fd. Next time around the loop, when the kernel signals there's space again (because the other side consumed data and sent ACKs via TCP sliding window), you pick up where you left off.

Two distinct layers are at work here. Epoll knows nothing about your application state — it only sees the kernel's buffers: is there data to read? Is there space to write? When you register `EPOLLOUT`, you're saying "tell me when I *can* write", not "I have a pending write". The kernel tells you *when*, your structure tells you *what*. Epoll wakes you up, you check your internal state, and resume.

### Why not just loop until a read or write completes? — Julio's question

Because while you're in a loop hammering `write()` for one client, everyone else is still waiting for an answer. And if the kernel buffer is full, non-blocking `write()` returns `EAGAIN` — looping on that is busy-wait, burning CPU doing nothing useful while waiting for the other side to consume data.

The model works because nobody monopolizes the loop. Each operation is short, and the saved state allows resuming any connection at any moment. The server is almost never computing; it's waiting. Epoll tells you exactly *who* is ready, you touch only those, and go back to waiting.

---

Link top top top top top top top TOP top demais, que recomendo, e que eu comecei a ler. O escritor é muito carismático e a leitura não é chata, mas o texto é longo:

    https://beej.us/guide/bgnet/html/

Explica um pouco de epoll, se quiser saber:

    https://medium.com/embedworld/building-a-scalable-event-driven-architecture-using-epoll-on-embedded-linux-bfd69fa39eae

    Ou, em vídeo:

    https://youtu.be/WuwUk7Mk80E?si=qRQGK5ehFPEDWFmj

Vídeo tutorial que explica como implementar muita coisa, se quiser ver:

    https://youtu.be/Kv2JERQR8AU?si=e7xbCp4DT8UjNY0N

epoll tutorial:

    https://medium.com/@m-ibrahim.research/mastering-epoll-the-engine-behind-high-performance-linux-networking-85a15e6bde90
