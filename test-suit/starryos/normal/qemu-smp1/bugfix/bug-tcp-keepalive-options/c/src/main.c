#define _GNU_SOURCE
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef TCP_USER_TIMEOUT
#define TCP_USER_TIMEOUT 18
#endif

static int passed;
static int failed;

static void note_pass(const char *name)
{
    printf("PASS: %s\n", name);
    passed++;
}

static void note_fail(const char *name, const char *detail)
{
    printf("FAIL: %s: %s\n", name, detail);
    failed++;
}

static void expect_set_int(int fd, int optname, int value, const char *name)
{
    errno = 0;
    int ret = setsockopt(fd, IPPROTO_TCP, optname, &value, sizeof(value));
    if (ret == 0) {
        note_pass(name);
        return;
    }

    char detail[160];
    snprintf(detail, sizeof(detail),
             "ret=%d errno=%d (%s), expected success",
             ret, errno, strerror(errno));
    note_fail(name, detail);
}

static void expect_invalid_value(int fd)
{
    int value = 0;
    errno = 0;
    int ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &value, sizeof(value));
    int saved_errno = errno;
    if (ret == -1 && saved_errno == EINVAL) {
        note_pass("TCP_KEEPIDLE rejects zero value");
        return;
    }

    char detail[160];
    snprintf(detail, sizeof(detail),
             "ret=%d errno=%d (%s), expected -1/EINVAL",
             ret, saved_errno, strerror(saved_errno));
    note_fail("TCP_KEEPIDLE zero value", detail);
}

static void expect_udp_rejects_tcp_keepalive(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        note_fail("create udp socket", strerror(errno));
        return;
    }

    int value = 30;
    errno = 0;
    int ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &value, sizeof(value));
    int saved_errno = errno;
    close(fd);

    if (ret == -1 && saved_errno == ENOPROTOOPT) {
        note_pass("UDP socket rejects TCP_KEEPIDLE with ENOPROTOOPT");
        return;
    }

    char detail[160];
    snprintf(detail, sizeof(detail),
             "ret=%d errno=%d (%s), expected -1/ENOPROTOOPT",
             ret, saved_errno, strerror(saved_errno));
    note_fail("UDP TCP_KEEPIDLE", detail);
}

int main(void)
{
    printf("=== bug-tcp-keepalive-options ===\n");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        note_fail("create tcp socket", strerror(errno));
    } else {
        expect_set_int(fd, TCP_KEEPIDLE, 60, "setsockopt TCP_KEEPIDLE");
        expect_set_int(fd, TCP_KEEPINTVL, 30, "setsockopt TCP_KEEPINTVL");
        expect_set_int(fd, TCP_KEEPCNT, 5, "setsockopt TCP_KEEPCNT");
        expect_set_int(fd, TCP_USER_TIMEOUT, 30000, "setsockopt TCP_USER_TIMEOUT");
        expect_invalid_value(fd);
        close(fd);
    }
    expect_udp_rejects_tcp_keepalive();

    printf("=== Results: %d passed, %d failed ===\n", passed, failed);
    if (failed == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("SOME TESTS FAILED\n");
    return 1;
}
