#include "automatic-llfree-balloon-tester.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>

// Experiment: Allocation or Consumation
#define ALLOC 0
#define CONSUME 1
#define ALLOC_MULTITHREAD 2
const char *experiment_names[] = { "ALLOC", "CONSUME" };

// Guest Frame Size: base (4KiB) or huge (2MiB)
#define BASE_FRAME 0
#define HUGE_FRAME 1
const char *gfs_names[] = { "BASE_FRAME", "HUGE_FRAME" };

// Host Backing: THP enabled or disabled
#define THP 0
#define NO_THP 1
const char *host_backing_names[] = { "THP", "NO_THP" };

// Host Backing: THP enabled or disabled
#define IODEVICE 0
#define VCPU_SYNC 1
const char *transport_mechanism_names[] = { "iodevice", "vcpu-sync" };

int main(int argc, char **argv)
{
	int file_desc;
	int num_experiment, num_gfs, num_host_backing, num_gib,
		num_transport_mechanism;
	char *logfile_path, *candidate_name;
	FILE *logfile;
	long ret;

	if (argc != 8) {
		printf("expected seven arguments, got %i\n", argc - 1);
		exit(EXIT_FAILURE);
	}

	logfile_path = argv[1];
	candidate_name = argv[2];
	num_experiment = atoi(argv[3]);
	num_gfs = atoi(argv[4]);
	num_host_backing = atoi(argv[5]);
	num_transport_mechanism = atoi(argv[6]);
	num_gib = atoi(argv[7]);

	if (!candidate_name) {
		printf("invalid candidate name\n");
		exit(EXIT_FAILURE);
	}

	if (num_experiment < 0 || num_experiment > 2) {
		printf("invalid argument range num_experiment\n");
		exit(EXIT_FAILURE);
	}

	if (num_gfs < 0 || num_gfs > 1) {
		printf("invalid argument range num_gfs\n");
		exit(EXIT_FAILURE);
	}

	if (num_host_backing < 0 || num_host_backing > 1) {
		printf("invalid argument range num_host_backing\n");
		exit(EXIT_FAILURE);
	}

	if (num_transport_mechanism < 0 || num_transport_mechanism > 1) {
		printf("invalid argument range num_transport_mechanism\n");
		exit(EXIT_FAILURE);
	}

	if (num_gib <= 0 || num_gib > 8) {
		printf("invalid argument range num_gib\n");
		exit(EXIT_FAILURE);
	}

	file_desc = open(DEVICE_PATH, O_RDWR);
	if (file_desc < 0) {
		exit(EXIT_FAILURE);
	}

	logfile = fopen(logfile_path, "a");
	if (!logfile) {
		printf("could not open logfile\n");
		exit(EXIT_FAILURE);
	}

	if (num_experiment == ALLOC && num_gfs == BASE_FRAME) {
		ret = ioctl(file_desc, IOCTL_ALLOC_BASE_PAGE_TEST, num_gib);
	} else if (num_experiment == ALLOC && num_gfs == HUGE_FRAME) {
		ret = ioctl(file_desc, IOCTL_ALLOC_HUGE_PAGE_TEST, num_gib);
	} else if (num_experiment == CONSUME && num_gfs == BASE_FRAME) {
		ret = ioctl(file_desc, IOCTL_CONSUME_BASE_PAGE_TEST, num_gib);
	} else if (num_experiment == CONSUME && num_gfs == HUGE_FRAME) {
		ret = ioctl(file_desc, IOCTL_CONSUME_HUGE_PAGE_TEST, num_gib);
	} else if (num_experiment == ALLOC_MULTITHREAD) {
		ret = ioctl(file_desc, IOCTL_ALLOC_TEST_MULTITHREADED, num_gib);
	}

	// logging
	printf("Test returned %li\n", ret);
	fprintf(logfile, "%s;%s;%s;%s;%s;%i;%li\n", candidate_name,
		experiment_names[num_experiment], gfs_names[num_gfs],
		host_backing_names[num_host_backing],
		transport_mechanism_names[num_transport_mechanism], num_gib,
		ret);

	close(file_desc);
	return ret;

error:
	close(file_desc);
	exit(EXIT_FAILURE);
}
