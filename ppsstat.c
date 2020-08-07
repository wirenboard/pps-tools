/*
 * ppstest.c -- simple tool to monitor PPS timestamps
 *
 * Copyright (C) 2005-2007   Rodolfo Giometti <giometti@linux.it>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include "timepps.h"

static struct timespec offset_assert = {0, 0};

int find_source(char *path, pps_handle_t *handle, int *avail_mode)
{
	pps_params_t params;
	int ret;

	printf("trying PPS source \"%s\"\n", path);

	/* Try to find the source by using the supplied "path" name */
	ret = open(path, O_RDWR);
	if (ret < 0) {
		fprintf(stderr, "unable to open device \"%s\" (%m)\n", path);
		return ret;
	}

	/* Open the PPS source (and check the file descriptor) */
	ret = time_pps_create(ret, handle);
	if (ret < 0) {
		fprintf(stderr, "cannot create a PPS source from device "
				"\"%s\" (%m)\n", path);
		return -1;
	}
	printf("found PPS source \"%s\"\n", path);

	/* Find out what features are supported */
	ret = time_pps_getcap(*handle, avail_mode);
	if (ret < 0) {
		fprintf(stderr, "cannot get capabilities (%m)\n");
		return -1;
	}
	if ((*avail_mode & PPS_CAPTUREASSERT) == 0) {
		fprintf(stderr, "cannot CAPTUREASSERT\n");
		return -1;
	}

	/* Capture assert timestamps */
	ret = time_pps_getparams(*handle, &params);
	if (ret < 0) {
		fprintf(stderr, "cannot get parameters (%m)\n");
		return -1;
	}
	params.mode |= PPS_CAPTUREASSERT;
	/* Override any previous offset if possible */
	if ((*avail_mode & PPS_OFFSETASSERT) != 0) {
		params.mode |= PPS_OFFSETASSERT;
		params.assert_offset = offset_assert;
	}
	ret = time_pps_setparams(*handle, &params);
	if (ret < 0) {
		fprintf(stderr, "cannot set parameters (%m)\n");
		return -1;
	}

	return 0;
}

int fetch_source(int i, pps_handle_t *handle, int *avail_mode)
{
	struct timespec timeout;
	pps_info_t infobuf;
	int ret;
	static struct timespec prev = {};
    static double pps_sum = 0;
    static double pps_sum_sq = 0;
    static int count = 0;
    
	/* create a zero-valued timeout */
	timeout.tv_sec = 3;
	timeout.tv_nsec = 0;

retry:
	if (*avail_mode & PPS_CANWAIT) /* waits for the next event */
		ret = time_pps_fetch(*handle, PPS_TSFMT_TSPEC, &infobuf,
				   &timeout);
	else {
		sleep(1);
		ret = time_pps_fetch(*handle, PPS_TSFMT_TSPEC, &infobuf,
				   &timeout);
	}
	if (ret < 0) {
		if (ret == -EINTR) {
			fprintf(stderr, "time_pps_fetch() got a signal!\n");
			goto retry;
		}

		fprintf(stderr, "time_pps_fetch() error %d (%m)\n", ret);
		return -1;
	}


    if ((prev.tv_sec != 0) || (prev.tv_nsec != 0)) {
        long long int delta = (infobuf.assert_timestamp.tv_sec - prev.tv_sec) * 1E9 + 
            (infobuf.assert_timestamp.tv_nsec - prev.tv_nsec);
        
        double pps = (delta * 1.0 - 1E9) / 1E3   ;
        count += 1;

        pps_sum += pps;
        pps_sum_sq += pps*pps;
        double var = pps_sum_sq / count - pps_sum / count * pps_sum / count;
        double std = sqrt(var);
            
        printf("diff %lld ns - "
                "pps %.2f - "
                "avg %.2f - "
                "std %.2f"
            "\n",
            delta,
            pps,
            pps_sum / count,
            std
            );
	        fflush(stdout);
    }
	prev  = infobuf.assert_timestamp;

	return 0;
}

void usage(char *name)
{
	fprintf(stderr, "usage: %s <ppsdev> [<ppsdev> ...]\n", name);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	pps_handle_t handle;
	int avail_mode;
	int i = 0;
	int ret;

	/* Check the command line */
	if (argc < 2)
		usage(argv[0]);

    ret = find_source(argv[1], &handle, &avail_mode);
    if (ret < 0) {
        exit(EXIT_FAILURE);
    }

	/* loop, printing the most recent timestamp every second or so */
	while (1) {
        i = 0;
        ret = fetch_source(i, &handle, &avail_mode);
        if (ret < 0 && errno != ETIMEDOUT)
            exit(EXIT_FAILURE);
	}

	for (; i >= 0; i--)
		time_pps_destroy(handle);

	return 0;
}

