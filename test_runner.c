#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <count_sons.h>

#define NR_CHILDREN (20)

static int sync_pipe[2];

static pid_t spawn_zombie_child() {
	pid_t ret = fork();

	if (ret == -1) {
		perror("fork");
		exit(1);
	} else if (ret == 0) {
		exit(0);
	}
	return ret;
}

static void sigusr1_handler(int signr) {
	exit(1);
}

static int do_child() {
	if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
		perror("signal");
		exit(1);
	}
	spawn_zombie_child();

	if (write(sync_pipe[1], " ", 1) == -1 && errno != EINTR) {
		perror("write");
		exit(1);
	}

	while (1) ;
	return 0;
}

static pid_t spawn_waiting_child() {
	pid_t ret = fork();

	if (ret == -1) {
		perror("fork");
		exit(1);
	} else if (ret == 0) {
		exit(do_child());
	}

	return ret;
}

static void zombify_child(pid_t child) {
	if (kill(child, SIGUSR1)) {
		perror("kill");
		exit(1);
	}
}

static void wait_until_zombie(pid_t pid) {
	char stat_path[128];
	char read_buffer[128];
	int fd;
	char state;

	snprintf(stat_path, 128, "/proc/%u/stat", pid);
	do {
		if ((fd = open(stat_path, O_RDONLY)) == -1) {
			perror("open");
			exit(1);
		}

		if (read(fd, read_buffer, 128) < 0) {
			perror("read");
			exit(1);
		}
		close(fd);

		state = *(strchr(read_buffer, ')') + 2);
	} while (state != 'Z');
}

static void wait_on_child(pid_t child) {
	if (waitpid(child, NULL, 0) != child) {
		perror("waitpid");
		exit(1);
	}
}

#define ASSERT_EQUALS(first, second, fmt...) 				\
	do {								\
		int _f = (first);					\
		int _s = (second);					\
		printf(fmt); printf(" ");				\
		if (_f == _s)						\
			puts("PASS");					\
		else							\
			printf(" FAIL, %d != %d \n", _f, _s);		\
	} while (0)

int main(int argc, char **argv) {
	pid_t pids[NR_CHILDREN];
	pid_t parent_pid = getpid();
	size_t to_read;
	int i;
	int ret;
	unsigned int saved_errno;

	puts("Check error codes");
	ret = slow_count_sons(-1234);
	saved_errno = errno;
	ASSERT_EQUALS(ret, -1, "slow_count_sons(-1234)");
	ASSERT_EQUALS(saved_errno, EINVAL, "slow_count_sons(-1234) errno");

	ret = fast_count_sons(-1234);
	saved_errno = errno;
	ASSERT_EQUALS(ret, -1, "fast_count_sons(-1234)");
	ASSERT_EQUALS(saved_errno, EINVAL, "fast_count_sons(-1234) errno");

	ret = slow_count_sons(1 << 30); // On most kernels max pid is 32768
	saved_errno = errno;
	ASSERT_EQUALS(ret, -1, "slow_count_sons(1 << 30)");
	ASSERT_EQUALS(saved_errno, ESRCH, "slow_count_sons(-1234) errno");

	ret = fast_count_sons(1 << 30);
	saved_errno = errno;
	ASSERT_EQUALS(ret, -1, "fast_count_sons(1 << 30)");
	ASSERT_EQUALS(saved_errno, ESRCH, "fast_count_sons(-1234) errno");

	puts("Checking on swapper");
	ASSERT_EQUALS(slow_count_sons(0),
		      1,
		      "slow_count_sons %u", 0);
	ASSERT_EQUALS(fast_count_sons(0),
		      1,
		      "fast_count_sons %u", 0);

	puts("Checking on self w/o sons");
	ASSERT_EQUALS(slow_count_sons(parent_pid),
		      0,
		      "slow_count_sons %u", parent_pid);
	ASSERT_EQUALS(fast_count_sons(parent_pid),
		      0,
		      "fast_count_sons %u", parent_pid);

	if (pipe(sync_pipe)) {
		perror("pipe");
		exit(1);
	}

	for (i = 0; i < NR_CHILDREN; ++i)
		pids[i] = spawn_waiting_child();

	to_read = NR_CHILDREN;
	while (to_read) {
		char buffer[NR_CHILDREN];
		ssize_t ret = read(sync_pipe[0], buffer, to_read);
		if (ret == -1 && errno != EINTR) {
			perror("read");
			exit(1);
		} else if (ret > 0) {
			to_read -= ret;
		}
	}

	puts("Checking on self");
	ASSERT_EQUALS(slow_count_sons(parent_pid),
		      NR_CHILDREN,
		      "slow_count_sons %u", parent_pid);
	ASSERT_EQUALS(fast_count_sons(parent_pid),
		      NR_CHILDREN,
		      "fast_count_sons %u", parent_pid);

	puts("Checking living children");
	for (i = 0; i < NR_CHILDREN; ++i) {
		ASSERT_EQUALS(slow_count_sons(pids[i]),
			      1,
			      "slow_count_sons %d %u", i, pids[i]);
		ASSERT_EQUALS(fast_count_sons(pids[i]),
			      1,
			      "fast_count_sons %d %u", i, pids[i]);
	}

	for (i = 0; i < NR_CHILDREN; ++i)
		zombify_child(pids[i]);

	for (i = 0; i < NR_CHILDREN; ++i)
		wait_until_zombie(pids[i]);

	puts("Checking on zombie children");
	ASSERT_EQUALS(slow_count_sons(parent_pid),
		      NR_CHILDREN,
		      "slow_count_sons %u", parent_pid);
	ASSERT_EQUALS(fast_count_sons(parent_pid),
		      NR_CHILDREN,
		      "fast_count_sons %u", parent_pid);
	for (i = 0; i < NR_CHILDREN; ++i) {
		ASSERT_EQUALS(slow_count_sons(pids[i]),
			      0,
			      "slow_count_sons %d %u", i, pids[i]);
		ASSERT_EQUALS(fast_count_sons(pids[i]),
			      1,
			      "fast_count_sons %d %u", i, pids[i]);
	}

	puts("Checking self after waiting on all children");
	for (i = 0; i < NR_CHILDREN; ++i)
		wait_on_child(pids[i]);
	ASSERT_EQUALS(slow_count_sons(parent_pid),
		      0,
		      "slow_count_sons %u", parent_pid);
	ASSERT_EQUALS(fast_count_sons(parent_pid),
		      0,
		      "fast_count_sons %u", parent_pid);
	return 0;
}
