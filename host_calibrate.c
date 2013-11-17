/* License: Public domain.
 *
 * A free product developed by analog10 (http://analog10.com)
 * Check it out!
 * */

#define _BSD_SOURCE
#include <endian.h>
#include <math.h>
#include <termios.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>

#include "protocol.h"

enum {
	FIVE_ONES = 0x55
	,FOUR_ONES = 0x15
	,THREE_ONES = 0x05
	,TWO_ONES = 0x01
	,ONE_ONE = 0x00
};

static int keep_running = 1;
void sig_handler(int signum) {
	keep_running = 0;
}

int xmit_recv(int fd, char* dest, char send){
	fprintf(stderr, "TX : %02hhx\n", send);

	write(fd, &send, 1);

	/* Wait for ack or err. */

	struct pollfd pfd;
	pfd.events = POLLIN;
	pfd.fd = fd;

	/* Wait 500 ms for response. */
	int ret = poll(&pfd, 1, 1000);
	if(-1 == ret){
		fprintf(stderr, "err poll: %s\n", strerror(errno));
		return -1;
	}

	if(0 == ret){
		fprintf(stderr, "timed out...\n");
		return -2;
	}

	ret = read(fd, dest, 1);
	if(-1 == ret){
		fprintf(stderr, "read err: %s\n", strerror(errno));
		return -1;
	}

	if(OUT_RX_ERR == (*dest & 0xF0)){
		fprintf(stderr, "recv err: %02hhx\n", *dest);
		return -3;
	}

	fprintf(stderr, "RX : %02hhx\n", *dest);
	return 0;
}

int main(int argc, char **argv) {
	int ret;
	unsigned baudrate = 4800;
	uint32_t target_freq = 16000000;

	if(argc < 2){
		fprintf(stderr, "Usage: TTY_DEV_NODE [BAUD_RATE] [TARGET_FREQUENCY] \n");
		return 1;
	}

	/* Get the baudrate from the command line. */
	if(argc > 2){
		char* endptr;
		baudrate = strtoul(argv[2], &endptr, 0);
		if(*endptr){
			fprintf(stderr, "err invalid baud rate parameter\n");
			return 1;
		}
	}

	if(argc > 3){
		char* endptr;
		target_freq = strtoul(argv[3], &endptr, 0);
		if(*endptr || !target_freq){
			fprintf(stderr, "err invalid target frequency parameter\n");
			return 1;
		}
	}

	int fd = open(argv[1], O_RDWR);
	struct termios ser_options;
	ret = tcgetattr(fd, &ser_options);
	if(ret < 0){
		fprintf(stderr, "err tcgetattr: %s\n", strerror(errno));
		return 1;
	}

	/* Set Baud speed. */
	switch(baudrate){
		case 50:
			baudrate = B50;
			break;

		case 75:
			baudrate = B75;
			break;

		case 110:
			baudrate = B110;
			break;

		case 134:
			baudrate = B134;
			break;

		case 150:
			baudrate = B150;
			break;

		case 200:
			baudrate = B200;
			break;

		case 300:
			baudrate = B300;
			break;

		case 600:
			baudrate = B600;
			break;

		case 1200:
			baudrate = B1200;
			break;

		case 1800:
			baudrate = B1800;
			break;

		case 2400:
			baudrate = B2400;
			break;

		case 4800:
			baudrate = B4800;
			break;

		case 9600:
			baudrate = B9600;
			break;

		case 19200:
			baudrate = B19200;
			break;

		case 38400:
			baudrate = B38400;
			break;

		case 57600:
			baudrate = B57600;
			break;

		case 115200:
			baudrate = B115200;
			break;

		case 230400:
			baudrate = B230400;
			break;

		default:
			fprintf(stderr, "err bad baudrate\n");
			return 1;
	}

	ret = cfsetispeed(&ser_options, baudrate);
	if(ret < 0){
		fprintf(stderr, "err cfsetispeed: %s\n", strerror(errno));
		return 1;
	}

	ret = cfsetospeed(&ser_options, baudrate);
	if(ret < 0){
		fprintf(stderr, "err cfsetospeed: %s\n", strerror(errno));
		return 1;
	}

	/* Setup for raw binary comms. */
	cfmakeraw(&ser_options);

	/* Reset options for communications. */
	ser_options.c_cflag &= ~(PARODD | PARENB | CS5 | CS6 | CS7 | CS8 | CSTOPB);

	ret = tcsetattr(fd, TCSAFLUSH, &ser_options);
	if(ret < 0){
		fprintf(stderr, "err tcsetattr: %s\n", strerror(errno));
		return 1;
	}

	signal(SIGINT, sig_handler);

	/* Shift frequency up to its MSB.
	 * Reset the frequency register and assign the value. */
	unsigned bits_remaining = 32;
	while(!(target_freq & 0x80000000)){
		target_freq <<= 1;
		--bits_remaining;
	}

	/* Empty RX buffer. */
	{
		uint8_t buffer[1024];
		struct pollfd pfd;
		pfd.events = POLLIN;
		pfd.fd = fd;
		ret = 1024;
		while(1024 == ret){
			ret = poll(&pfd, 1, 500);
			if(-1 == ret){
				fprintf(stderr, "err poll: %s\n", strerror(errno));
				return -1;
			}

			ret = read(fd, buffer, ret);
		}
		if(-1 == ret){
			fprintf(stderr, "err emptying buff: %s\n", strerror(errno));
			return -1;
		}
	}

	uint8_t rx = 0;

	/* Reset register. */
	ret = xmit_recv(fd, &rx, FOUR_ONES);
	if(ret)
		return ret;
	if(rx != (OUT_RX_ACK | 4)){
		return -4;
	}

	/* Set register value. */
	while(bits_remaining){
		uint8_t xmit = (target_freq & 0x80000000)
			? ONE_ONE : TWO_ONES;
		ret = xmit_recv(fd, &rx, xmit);
		if(ret)
			return ret;

		/* Make sure we got proper ack back. */
		if((xmit == ONE_ONE) && rx != (OUT_RX_ACK | 0x1))
			return -4;

		if((xmit == TWO_ONES) && rx != (OUT_RX_ACK | 0x2))
			return -4;

		target_freq <<= 1;
		--bits_remaining;
	}

	/* Register is set, now initiate calibration. */
	ret = xmit_recv(fd, &rx, THREE_ONES);
	if(ret)
		return ret;
	if(rx != (OUT_RX_ACK | 0x3))
		return -4;

	/* Read dco, bcs values. */
	struct {
		uint32_t estimate;
		uint32_t variance;
		uint8_t dco;
		uint8_t bcs;
	} result;

	while(keep_running){
		ret = xmit_recv(fd, &rx, 0x55);
		if(ret)
			return ret;

		if(OUT_MAX == rx || OUT_MIN == rx){
			fprintf(stderr, (OUT_MIN == rx)
					? "Cannot reach frequency; too low\n"
					: "Cannot reach frequency; too high\n");

			unsigned count = 0;
			while(count < 10){
				ret = read(fd, (uint8_t*)(&result) + count, sizeof(result)  - count);
				count += ret;
			}

			result.estimate = le32toh(result.estimate);
			fprintf(stderr, "\n\nDCO  BCS1CTL  ESTIMATE  VARIANCE\n");
			printf("%02hhx   %02hhx       %8u  %8.1lf\n"
				,result.dco ,result.bcs ,result.estimate ,(double)result.variance / 10.0);

			fprintf(stderr, "\n\nSTDDEV: %lf\n", sqrt((double)result.variance / 10.0));
			break;
		}
		else if(OUT_FINISH == rx){

			unsigned count = 0;
			while(count < 10){
				ret = read(fd, (uint8_t*)(&result) + count, sizeof(result)  - count);
				count += ret;
			}

			result.estimate = le32toh(result.estimate);
			fprintf(stderr, "\n\nDCO  BCS1CTL  ESTIMATE  VARIANCE\n");
			printf("%02hhx   %02hhx       %8u  %8.1lf\n"
				,result.dco ,result.bcs ,result.estimate ,(double)result.variance / 10.0);

			fprintf(stderr, "\n\nSTDDEV: %lf\n", sqrt((double)result.variance / 10.0));
			break;
		}
		else if(OUT_MOD_INCREMENT == (rx & 0xF0)){
			usleep(100);
		}
		else if(OUT_MOD_DECREMENT == (rx & 0xF0)){
			usleep(100);
		}
		else{
			fprintf(stderr, "BAD RX %02hhx\n", rx);
			break;
		}

		usleep(100);
	}

	signal(SIGINT, SIG_DFL);

	return 0;
}
