#include "metronome.h"

/***************************GLOBALS*********************************************************************/
metronome_t metronome;
ioattr_t ioattr[NUM_DEVICES]; // I/O attribute structure, attributes associated to Resource Manager
int server_coid;
char data[255];
int quit;

int main(int argc, char *argv[]) {
	dispatch_t * dpp; // pointer to Dispatch thread
	resmgr_io_funcs_t io_funcs; // POSIX IO functions
	resmgr_connect_funcs_t connect_funcs; // POSIX connection functions
	dispatch_context_t * ctp; //pointer to thread context
	name_attach_t * attach_name;

	pthread_attr_t attr; // Thread Attribute object
	pthread_t metro_thread; //thread

	int resmgr_id; //generic return value variable
	quit = 0;

	// Verify 4 argument parameters program, bpm, tst, tsb.
	if (argc != 4) {
		usage();
		exit(EXIT_FAILURE);
	}

	// Assign command line arguments to appropriate metronome properties
	metronome.metro_props.bpm = atoi(argv[1]);
	metronome.metro_props.tst = atoi(argv[2]);
	metronome.metro_props.tsb = atoi(argv[3]);

	// check initial settings, if invalid exit
	if(search_data_table(&metronome) == ERROR){
		fprintf(stderr, "Invalid metronome settings\n");
		exit (EXIT_FAILURE);
	}

	// create dispatch handler
	if ((dpp = dispatch_create()) == NULL) {
		fprintf(stderr, "%s:  Unable to create dispatch handler.\n", argv[0]);
		return (EXIT_FAILURE);
	}

	// Point and mount to overridden functions
	iofunc_funcs_t metro_ocb_funcs = { _IOFUNC_NFUNCS, metro_ocb_calloc, metro_ocb_free, };
	iofunc_mount_t metro_mount = { 0, 0, 0, 0, &metro_ocb_funcs };

	// Initialize POSIX-layer functions
	iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
	connect_funcs.open = io_open;
	io_funcs.read = io_read;
	io_funcs.write = io_write;

	// attach name for each device resource manager
	for (int i = 0; i < NUM_DEVICES; i++) {

		// Initialize attribute structure with Resource Manager */
		iofunc_attr_init(&ioattr[i].attr, S_IFCHR | 0666, NULL, NULL);
		ioattr[i].device = i;
		ioattr[i].attr.mount = &metro_mount;

		// attach a name (path) to the resource manager
		// dpp = dispatch thread pointer
		// Ftype_Any = constant to indicate any filetype
		// 0 = file creation mode. Create if does not already exist
		// connect and I/O function tables
		if ((resmgr_id = resmgr_attach(dpp, NULL, device_names[i], _FTYPE_ANY, 0, &connect_funcs, &io_funcs, &ioattr[i])) == ERROR) {
			fprintf(stderr, "%s:  Unable to attach name.\n", argv[0]);
			return (EXIT_FAILURE);
		}
	}

	// allocate dispatch context
	ctp = dispatch_context_alloc(dpp);

	// create a named controllers channel
	if ((attach_name = name_attach(NULL, METRO_ATTACH, 0)) == NULL) {
		fprintf(stderr, "%s:  Unable to attach name.\n", METRO_ATTACH);
		exit(EXIT_FAILURE);
	}

	// intialize thread attribute object
	pthread_attr_init(&attr);
	// create thread for metronome and pass metronome data to the thread function
	pthread_create(&metro_thread, &attr, &metronome_thread, attach_name);
	pthread_attr_destroy(&attr); // destroy thread attributes

	// listen for messages from clients
	while (quit !=1) {

		// block thread until a message is received
		ctp = dispatch_block(ctp);

		// if message received is valid process it with handler
		if(ctp != NULL ){
			dispatch_handler(ctp);
		}

		else{
			fprintf(stderr, "ERROR: Dispatch message invalid");
		}

	}

	// Wait for thread to be destroyed then join and cleanup
	pthread_join(metro_thread, NULL);

	// Close connection to channel
	if((name_close(server_coid)) == ERROR) {
		perror("Close connection to channel failed\n");
		return EXIT_FAILURE;
	}

	// Detach/Destroy channel
	if((name_detach(attach_name, 0)) == ERROR) {
		perror("Close channel failed\n");
		return EXIT_FAILURE;
	}

	// Detach/Destroy resource manager namespace
	if((resmgr_detach(dpp, resmgr_id, 0)) == ERROR){
		perror("Resmgr detach failed\n");
		return EXIT_FAILURE;
	}

	printf("\nProgram Terminated gracefully");
	return EXIT_SUCCESS;

}

/******************************Function implementation******************************************************/


/************************************************************************************************************
 * Function: *metronome_thread
 * Description: thread function to drive metronome. Receives pulse's from interval timer and io_write POSSIX
 * function
 * return: void
 ************************************************************************************************************/
void *metronome_thread(void* arg) {

	message_t msg;
	struct sigevent event;
	struct itimerspec itime;
	struct itimerspec stop_itime;
	timer_t timer_id;
	name_attach_t *attach_name = arg;
	char * signature_p;
	int signature_index;
	int rcvid;

	// configure stop_itime as to preserve calculated time struct
	stop_itime.it_value.tv_sec = 0; // set expiration to 0 to stop timer
	stop_itime.it_value.tv_nsec = 0;
	stop_itime.it_interval.tv_sec = 0;
	stop_itime.it_interval.tv_nsec = 0;

	/********************************* PHASE 1 ***************************************************************/

	// configure a signal event to schedule and receive periodic pulses
	event.sigev_notify = SIGEV_PULSE; // set event to pulse notification
	event.sigev_coid = ConnectAttach(ND_LOCAL_NODE, 0, attach_name->chid, _NTO_SIDE_CHANNEL, 0); // attach metronome to named channel
	event.sigev_code = METRONOME_PULSE_CODE; //set pulse code to default

	// create a timer attached to the configured signal event
	timer_create(CLOCK_MONOTONIC, &event, &timer_id);

	signature_index = search_data_table(&metronome);
	signature_p = t[signature_index].pattern;

	// calculate the interval timer for the settings provided ( exit check performed in main, shouldn't require one here )
	itime = calculate_timer(&metronome);
	start_timer(&itime, timer_id, &metronome);

	/******************************* PHASE 2 ********************************************************************/

	for (;;) {

		// Listen for MSG
		if ((rcvid = MsgReceive(attach_name->chid, &msg, sizeof(msg), NULL)) == ERROR) {
			fprintf(stderr, "ERROR: Message receive operation has failed ");
			exit(EXIT_FAILURE);
		}

		if (rcvid == PULSE) {

			switch (msg.pulse.code) {

			// regular interval pulse from timer
			case METRONOME_PULSE_CODE:

				if (*signature_p == '|') {
					printf("%.2s", signature_p);
					signature_p += 2;
				}

				else if (*signature_p == '\0') {
					printf("\n");
					signature_p = t[signature_index].pattern;
				}

				else {
					printf("%c", *signature_p++);
				}
				break;

			case START_PULSE_CODE:

				// If time is currently stopped, start timer
				if (metronome.timer_props.status == STOPPED) {
					start_timer(&itime, timer_id, &metronome);
				}
				break;

			case STOP_PULSE_CODE:

				// If timer is currently active or is paused, stop timer
				if (metronome.timer_props.status == STARTED || metronome.timer_props.status == PAUSED) {
					stop_timer(&stop_itime, timer_id, &metronome);
				}
				break;

			// pulse code for pausing the timer
			case PAUSE_PULSE_CODE:

				// If timer is currently active pause the timer
				if (metronome.timer_props.status == STARTED) {
					pause_timer(&itime, timer_id, &msg);
				}
				break;

			case SET_PULSE_CODE:

				signature_index = search_data_table(&metronome);
				if(signature_index == ERROR){
					printf("Invalid time signature please set another signature\n");
					stop_timer(&itime, timer_id, &metronome);
					break;
				}

				signature_p = t[signature_index].pattern;
				itime = calculate_timer(&metronome);
				start_timer(&itime, timer_id, &metronome);
				printf("\n");
				break;

			case QUIT_PULSE_CODE:

				//remove timer, detach from name-space and remove connection to server
				timer_delete(timer_id);
				pthread_exit(NULL);
				break;
			}
		}
		fflush(stdout);

	}
	return NULL;
}

/************************************************************************************************************
 * Function: find_valid_signature
 * Description: function to compare metronome properties to valid time signatures in table
 * return: signature_index of time signature, or ERROR
 ************************************************************************************************************/
int search_data_table(metronome_t * metronome) {

	for (int i = 0; i <= 7; i++) {
		if (t[i].tsb == metronome->metro_props.tsb && t[i].tst == metronome->metro_props.tst) {
			return i;
		}
	}

	return ERROR;
}

/************************************************************************************************************
 * Function: calculateTimer
 * Description: Configure timer to the appropriate interval based on BPM
 * return: void
 ************************************************************************************************************/
struct itimerspec calculate_timer(metronome_t* metronome){
	double calcBps, measure, seconds, decimalSec, nanoSeconds;
	struct itimerspec itime;

	calcBps = (double)60 / metronome->metro_props.bpm;					// Calculate metronome beats per min to beats per second
	measure = (double)calcBps *  metronome->metro_props.tst;			// Calculate time between each measure
	seconds = (double)measure / metronome->metro_props.tsb;				// Calculate seconds between each interval
	decimalSec = seconds - (int)seconds;								// Get the decimal value from double seconds

	// Calculate the nanoSeconds for the timer = decimal value x 10^9
	for (int i = 0; i < 9; i++)
		nanoSeconds = (double)(decimalSec *= 10);

	// Set starting and interval times
	itime.it_value.tv_sec = (int)seconds;
	itime.it_value.tv_nsec = nanoSeconds;
	itime.it_interval.tv_sec = (int)seconds;
	itime.it_interval.tv_nsec = nanoSeconds;

	return itime;
}




/************************************************************************************************************
 * Function: start_timer
 * Description: start interval timer
 * return: void
 ************************************************************************************************************/
void start_timer(struct itimerspec * itime, timer_t timer_id, metronome_t* metronome) {
	timer_settime(timer_id, 0, itime, NULL); //update timer
	metronome->timer_props.status = STARTED;
}


/************************************************************************************************************
 * Function: stop_timer
 * Description: stop the current timer
 * return: void
 ************************************************************************************************************/
void stop_timer(struct itimerspec * stop_itime, timer_t timer_id, metronome_t * metronome) {
	timer_settime(timer_id, 0, stop_itime, NULL); //update timer
	metronome->timer_props.status = STOPPED;
}

/************************************************************************************************************
 * Function: pause_timer
 * Description: pause the current timer
 * return: void
 ************************************************************************************************************/
void pause_timer(struct itimerspec * itime, timer_t timer_id, message_t * msg) {
	itime->it_value.tv_sec = msg->pulse.value.sival_int;
	timer_settime(timer_id, 0, itime, NULL);
}


/************************************************************************************************************
 * Function: io_read
 * Description: Overridden POSIX I/O read function
 * return: void
 ************************************************************************************************************/
int io_read(resmgr_context_t *ctp, io_read_t *msg, metro_ocb_t *mocb) {

	int signature_index;
	int nb;

	if (data == NULL)
		return 0;

	// if reading from metronome_help device name space print a help message
	if (mocb->ocb.attr->device == METRONOME_HELP) {
		sprintf(data, "metronome Resource Manager (Resmgr)\n\nUsage: metronome <bpm> <ts-top> <ts-bottom>\n\nAPI:\n pause[1-9]\t\t\t-pause the metronome for 1-9 seconds\n quit:\t\t\t\t- quit the metronome\n set <bpm> <ts-top> <ts-bottom>\t- set the metronome to <bpm> ts-top/ts-bottom\n start\t\t\t\t- start the metronome from stopped state\n stop\t\t\t\t- stop the metronome; use 'start' to resume\n");
	}

	// read from metronome name space
	else {

		signature_index = search_data_table(&metronome);

		sprintf(data,
				"[metronome: %d beats/min, time signature: %d/%d, sec-per-interval: %.2f, nanoSecs: %.0lf]\n",
				metronome.metro_props.bpm, t[signature_index].tst,
				t[signature_index].tsb, metronome.timer_props.interval,
				metronome.timer_props.nano_sec);
	}
	nb = strlen(data);

	//test to see if we have already sent the whole message.
	if (mocb->ocb.offset == nb)
		return 0;

	//We will return which ever is smaller the size of our data or the size of the bufferfer
	nb = min(nb, msg->i.nbytes);

	//Set the number of bytes we will return
	_IO_SET_READ_NBYTES(ctp, nb);

	//Copy data into reply buffer.
	SETIOV(ctp->iov, data, nb);

	//update offset into our data used to determine start position for next read.
	mocb->ocb.offset += nb;

	//If we are going to send any bytes update the access time for this resource.
	if (nb > 0)
		mocb->ocb.flags |= IOFUNC_ATTR_ATIME;

	return (_RESMGR_NPARTS(1));
}

/***************************************
 * Function : io_write
 * Description: POSIX
 ****************************************/
int io_write(resmgr_context_t *ctp, io_write_t *msg, metro_ocb_t *mocb) {

	int nb = 0;

	if (mocb->ocb.attr->device == METRONOME_HELP) {
		fprintf(stderr, "ERROR: Cannot Write to device \n");
		nb = msg->i.nbytes;
		_IO_SET_WRITE_NBYTES(ctp, nb);
		return (_RESMGR_NPARTS(0));
	}

	if (msg->i.nbytes == ctp->info.msglen - (ctp->offset + sizeof(*msg))) {

		char *buffer;
		char * pause_msg;
		char * set_msg;
		int small_integer = 0;
		buffer = (char *) (msg + 1);

		// if command is pause, validate integer and send pulse
		if (strstr(buffer, "pause") != NULL) {
			for (int i = 0; i < 2; i++) {
				pause_msg = strsep(&buffer, " ");
			}

			small_integer = atoi(pause_msg);
			//validate seconds argument to be 1-9 inclusive
			if (small_integer >= 1 && small_integer <= 9) {
				MsgSendPulse(server_coid, SchedGet(0, 0, NULL), PAUSE_PULSE_CODE, small_integer);
			}
			else {
				printf("Integer is not between 1 and 9.\n");
			}

		}

		//if command is start send appropriate pulse
		else if (strstr(buffer, "start") != NULL) {
			MsgSendPulse(server_coid, SchedGet(0, 0, NULL), START_PULSE_CODE, small_integer);
		}

		//if command is stop send appropriate pulse
		else if (strstr(buffer, "stop") != NULL) {
			MsgSendPulse(server_coid, SchedGet(0, 0, NULL), STOP_PULSE_CODE,small_integer);
		}

		//if command is set, then set all metronome properties to new values and send appropriate pulse
		else if (strstr(buffer, "set") != NULL) {
			for (int i = 0; i < 4; i++) {
				set_msg = strsep(&buffer, " ");

				if (i == 1) {
					metronome.metro_props.bpm = atoi(set_msg);
				}

				else if (i == 2) {
					metronome.metro_props.tst = atoi(set_msg);
				}

				else if (i == 3) {
					metronome.metro_props.tsb = atoi(set_msg);
				}
			}
			MsgSendPulse(server_coid, SchedGet(0, 0, NULL), SET_PULSE_CODE, small_integer);
		}

		//if command is stop send appropriate pulse
		else if (strstr(buffer, "quit") != NULL) {
			MsgSendPulse(server_coid, SchedGet(0, 0, NULL), QUIT_PULSE_CODE, small_integer);
			quit = 1;
			printf("Metronome is going to be stopped.....\n");
		}

		// if any other command is encounter print error message
		else {
			fprintf(stderr, "ERROR: previous entry is not a valid command\n");
			strcpy(data, buffer);
		}

		nb = msg->i.nbytes;
	}

	_IO_SET_WRITE_NBYTES(ctp, nb);

	if (msg->i.nbytes > 0)
		mocb->ocb.flags |= IOFUNC_ATTR_MTIME | IOFUNC_ATTR_CTIME;

	return (_RESMGR_NPARTS(0));
}

/************************************************************************************************************
 * Function: io_open
 * Description: POSIX name space connection function
 * return: iofunc_open_default or FAILURE
 ************************************************************************************************************/
int io_open(resmgr_context_t *ctp, io_open_t *msg, RESMGR_HANDLE_T *handle, void *extra) {

	if ((server_coid = name_open(METRO_ATTACH, 0)) == ERROR) {
		perror("ERROR - name_open failed - io_open() \n ");
		fprintf(stderr, "%s: ERROR: Unable to open name space", METRO_ATTACH);
		return EXIT_FAILURE;
	}
	return (iofunc_open_default(ctp, msg, &handle->attr, extra));
}

/************************************************************************************************************
 * Override IOFUNC_OCB_T ocb calloc()
 ************************************************************************************************************/
metro_ocb_t * metro_ocb_calloc(resmgr_context_t *ctp, ioattr_t *mattr) {
	metro_ocb_t *mocb;
	mocb = calloc(1, sizeof(metro_ocb_t));
	mocb->ocb.offset = 0;
	return (mocb);
}

/************************************************************************************************************
 * Override IOFUNC_OCB_T free()
 ************************************************************************************************************/
void metro_ocb_free(metro_ocb_t *mocb) {
	free(mocb);
}

/************************************************************************************************************
 * Function: usage
 * Description: message to show command line program instructions.
 * return: void
 ************************************************************************************************************/
void usage() {
	printf("Command: ./metronome <beats/minute> <time-signature-top> <time-signature-bottom>\n");
}
