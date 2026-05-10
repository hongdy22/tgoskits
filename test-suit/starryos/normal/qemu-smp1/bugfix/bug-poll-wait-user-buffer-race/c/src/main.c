/*
 * bug-poll-wait-user-buffer-race: poll must not keep direct kernel references
 * to the user pollfd array or revents fields while it blocks.
 *
 * Old behavior: poll validated the user pollfd array before sleeping, kept
 * mutable references to each revents field, and later wrote through them
 * directly. If another thread unmapped that array while poll was blocked, the
 * kernel took a page fault when an event woke the waiter.
 *
 * Fixed behavior: poll waits using a kernel pollfd copy and copies the final
 * revents back with checked user-copy. If the userspace array disappears, the
 * syscall returns EFAULT instead of panicking the kernel.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

static _Atomic int waiter_entered = 0;

struct waiter_context {
    struct pollfd *fds;
    int result;
    int error;
};

static void *waiter_thread(void *arg)
{
    struct waiter_context *ctx = arg;

    atomic_store_explicit(&waiter_entered, 1, memory_order_release);
    errno = 0;
    ctx->result = poll(ctx->fds, 1, 5000);
    ctx->error = errno;
    return NULL;
}

static int wait_for_waiter_to_block(void)
{
    const struct timespec settle = {
        .tv_sec = 0,
        .tv_nsec = 100 * 1000 * 1000,
    };

    while (atomic_load_explicit(&waiter_entered, memory_order_acquire) == 0) {
        sched_yield();
    }

    return nanosleep(&settle, NULL);
}

int main(void)
{
    printf("=== bug-poll-wait-user-buffer-race ===\n");
    printf("Starting poll, unmapping its pollfd array, then waking it...\n");

    int event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (event_fd < 0) {
        printf("eventfd failed: errno=%d (%s)\n", errno, strerror(errno));
        return 1;
    }

    const size_t page_size = 4096;
    struct pollfd *fds = mmap(
        NULL,
        page_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );
    if (fds == MAP_FAILED) {
        printf("mmap failed: errno=%d (%s)\n", errno, strerror(errno));
        close(event_fd);
        return 1;
    }

    fds[0].fd = event_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    struct waiter_context ctx = {
        .fds = fds,
        .result = 0,
        .error = 0,
    };

    pthread_t waiter;
    int err = pthread_create(&waiter, NULL, waiter_thread, &ctx);
    if (err != 0) {
        printf("pthread_create failed: errno=%d (%s)\n", err, strerror(err));
        munmap(fds, page_size);
        close(event_fd);
        return 1;
    }

    if (wait_for_waiter_to_block() != 0) {
        printf("nanosleep failed: errno=%d (%s)\n", errno, strerror(errno));
        return 1;
    }

    if (munmap(fds, page_size) != 0) {
        printf("munmap failed: errno=%d (%s)\n", errno, strerror(errno));
        return 1;
    }

    uint64_t one = 1;
    if (write(event_fd, &one, sizeof(one)) != (ssize_t)sizeof(one)) {
        printf("eventfd write failed: errno=%d (%s)\n", errno, strerror(errno));
        return 1;
    }

    err = pthread_join(waiter, NULL);
    if (err != 0) {
        printf("pthread_join failed: errno=%d (%s)\n", err, strerror(err));
        return 1;
    }

    close(event_fd);

    if (ctx.result != -1 || ctx.error != EFAULT) {
        printf(
            "expected poll to fail with EFAULT, got result=%d errno=%d (%s)\n",
            ctx.result,
            ctx.error,
            strerror(ctx.error)
        );
        printf("TEST FAILED\n");
        return 1;
    }

    printf("poll returned EFAULT after the pollfd array was unmapped\n");
    printf("ALL TESTS PASSED\n");
    return 0;
}
