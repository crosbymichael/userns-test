#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

// root  uid:gid for the user on the host machine to setup a new
// user namespace for
#define ROOT_ID 1000

#define STACKSIZE (1024*1024)
static char child_stack[STACKSIZE];

struct clone_args {
	char **argv;
	int pipe[2];
};

// move the eth0 into the childs namespace
// 
// this still needs the following commands to get a routable interface
//
// ip link set dev lo up
// ip link set dev eth0 down
// ip addr add 172.17.0.14/16 dev eth0
// ip link set dev eth0 up
// ip route add default via 172.17.42.1
int move_eth0(pid_t pid)
{
	char spid[24] = { 0x0 };
	sprintf(spid, "%d", pid);

	int fpid = fork();
	if (fpid < 0) {
		fprintf(stderr, "ork %s\n", strerror(errno));
		return 1;
	}

	if (fpid == 0) {
		execl("/bin/network", "network", spid, NULL);
		fprintf(stderr, "execl %s\n", strerror(errno));
		exit(1);
	}

	if (waitpid(fpid, NULL, 0) == -1) {
		fprintf(stderr, "waitpid %s\n", strerror(errno));
		return 1;
	}

	return 0;
}

int change_user(int id)
{
	if (setuid(id) != 0) {
		return 1;
	}

	if (setgid(id) != 0) {
		return 1;
	}

	return 0;
}

static int child_exec(void *stuff)
{
	struct clone_args *args = (struct clone_args *)stuff;
	char buf;

	// close child side of the pipe
	close(args->pipe[1]);

	if (read(args->pipe[0], &buf, 1) != 0) {
		fprintf(stderr, "failed to sync with parent process %s\n",
			strerror(errno));
		exit(-1);
	}
	// change back to root now that we are in our own namespace
	if (change_user(0) != 0) {
		fprintf(stderr, "change uid:gid to 0 %s\n", strerror(errno));
		exit(-1);
	}

	if (execvp(args->argv[0], args->argv) != 0) {
		fprintf(stderr, "failed to execvp argments %s\n",
			strerror(errno));
		exit(-1);
	}

	exit(EXIT_FAILURE);
	return 0;
}

int write_map(char *path, char *value)
{
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		return 1;
	}

	size_t l = strlen(value);
	if (write(fd, value, l) != l) {
		close(fd);

		return 1;
	}

	close(fd);

	return 0;
}

int main(int argc, char **argv)
{
	struct clone_args args;
	pid_t pid;
	// inner user | outer user | length
	char *value = "0 1000 65000";
	args.argv = &argv[1];

	if (pipe(args.pipe) == -1) {
		fprintf(stderr, "unable to create sync pipe %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}
	// we need NEWNET so that we can create and manage our container's network interfaces
	// NETNS so that we can setup conatiner mounts and such 
	// TODO: getting an mknod error
	pid =
	    clone(child_exec, child_stack + STACKSIZE,
		  CLONE_NEWUSER | CLONE_NEWNET | CLONE_NEWNS | SIGCHLD, &args);

	if (pid < 0) {
		fprintf(stderr, "clone into new user namespace %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}
	// write the uid:gid mappings to the processes files
	char id_path[PATH_MAX];
	snprintf(id_path, PATH_MAX, "/proc/%d/uid_map", pid);

	if (write_map(id_path, value) != 0) {
		fprintf(stderr, "failed to write id map to %s %s\n", id_path,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	snprintf(id_path, PATH_MAX, "/proc/%d/gid_map", pid);
	if (write_map(id_path, value) != 0) {
		fprintf(stderr, "failed to write id map to %s %s\n", id_path,
			strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (move_eth0(pid) != 0) {
		exit(EXIT_FAILURE);
	}
	// signal to the child process that we are finished writing the uid:gid mapping
	close(args.pipe[1]);

	if (waitpid(pid, NULL, 0) == -1) {
		fprintf(stderr, "failed to wait pid %d\n", pid);
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
