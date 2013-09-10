/* License: Public domain. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <ftdi.h>

static int keep_running = 1;
void sig_handler(int signum) {
	keep_running = 0;
}

int main(int argc, char **argv) {
	struct ftdi_context *ftdi;
	int ret;
	unsigned baudrate = 9600;

	/* Get the baudrate from the command line. */
	if(argc > 1){
		char* endptr;
		baudrate = strtoul(argv[1], &endptr, 0);
		if(*endptr){
			fprintf(stderr, "err invalid baud rate parameter\n");
			return 1;
		}
	}

	if((ftdi = ftdi_new()) == 0){
		fprintf(stderr, "err ftdi_new\n");
		return 1;
	}

	// Select interface
	ftdi_set_interface(ftdi, INTERFACE_ANY);

	/* Using FT230X here, obtained from lsusb. */
	ret = ftdi_usb_open(ftdi, 0x403, 0x6015);

	if(ret < 0){
		fprintf(stderr, "err opening : %i %s\n", ret, ftdi_get_error_string(ftdi));
		return 1;
	}

	// Set baudrate
	ret = ftdi_set_baudrate(ftdi, baudrate);
	if(ret < 0){
		fprintf(stderr, "err set baudrate: %d (%s)\n", ret, ftdi_get_error_string(ftdi));
		return 1;
	}

	signal(SIGINT, sig_handler);

	uint8_t output = 0x55;
	while(keep_running){
		/* Wait for init packet. */
		uint8_t rx;
		ret = ftdi_read_data(ftdi, &rx, 1);
		if(ret < 0){
			fprintf(stderr, "Read error");
			return 1;
		}

		fprintf(stderr, "Got %02hhx, %i\n", rx, ret);

		if(0xcc == rx){
			/* Read dco, bcs values. */
			uint8_t vals[2];
			unsigned count = 0;
			while(count < 2){
				ret = ftdi_read_data(ftdi, vals + count, 2 - count);
				fprintf(stderr, "Read back %i\n", ret);
				count += ret;
			}
			fprintf(stderr, "DCO  BCS1CTL\n");
			printf("%02hhx    %02hhx\n", vals[0], vals[1]);
			break;
		}

		usleep(100);

		/* Write  */
		ret = ftdi_write_data(ftdi, &output, 1);
		if(ret < 0){
			fprintf(stderr, "Write error");
			return 1;
		}
	}

	signal(SIGINT, SIG_DFL);

	ftdi_usb_close(ftdi);
do_deinit:
	ftdi_free(ftdi);

	return 0;
}
