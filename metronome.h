#ifndef SRC_METRONOME_H_
#define SRC_METRONOME_H_

// declare overrides before includes
struct ioattr_t;
#define IOFUNC_ATTR_T struct ioattr_t
struct metro_ocb;
#define IOFUNC_OCB_T struct metro_ocb

/***************************************************************************************************************
 * Library imports
 ***************************************************************************************************************/
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <math.h>
#include <sys/types.h>
#include <sys/netmgr.h>
#include <sys/neutrino.h>
#include <sys/wait.h>
#include <errno.h>

/***************************************************************************************************************
 * Macro constants
 ***************************************************************************************************************/
#define DATA_ROWS 8
#define DATA_COLS 4

//pulse codes
#define DEFAULT_PULSE_CODE _PULSE_CODE_MINAVAIL
#define METRONOME_PULSE_CODE (DEFAULT_PULSE_CODE +1)
#define START_PULSE_CODE (DEFAULT_PULSE_CODE +2)
#define STOP_PULSE_CODE (DEFAULT_PULSE_CODE +3)
#define PAUSE_PULSE_CODE  (DEFAULT_PULSE_CODE +4)
#define SET_PULSE_CODE  (DEFAULT_PULSE_CODE +5)
#define QUIT_PULSE_CODE   (DEFAULT_PULSE_CODE +6)

// name space attach
#define METRO_ATTACH  "metronome"

// timer status macros
#define STARTED 0
#define STOPPED 1
#define PAUSED 2

// device macros
#define METRONOME 0
#define METRONOME_HELP 1
#define NUM_DEVICES 2

// return macros
#define PULSE 0
#define ERROR -1

/***************************************************************************************************************
 * Structures
 ***************************************************************************************************************/

// message structure
typedef union {
	struct _pulse pulse;
	char msg[255];
}message_t;

// individual rows for metronome sequence table
struct DataTableRow{
	int tst;
	int tsb;
	int intervals;
	char pattern[16];
};

// metronome signature table
struct DataTableRow t[] = {
		{2, 4, 4, "|1&2&"},
		{3, 4, 6, "|1&2&3&"},
		{4, 4, 8, "|1&2&3&4&"},
		{5, 4, 10, "|1&2&3&4-5-"},
		{3, 8, 6, "|1-2-3-"},
		{6, 8, 6, "|1&a2&a"},
		{9, 8, 9, "|1&a2&a3&a"},
		{12, 8, 12, "|1&a2&a3&a4&a"}
};

// metronome properties representing time signature settings
struct Metronome_Properties {
	int bpm; // beats per minute
	int tst; // top time signature
	int tsb; // bottom time signature
}typedef metro_props_t;

// timer properties to control metronome operations
struct Timer_Properties{
	double bps; // beats per second
	double measure; // beats per measure
	double interval; // Seconds per interval
	double nano_sec; // nanoseconds
	int status;
}typedef timer_props_t;

// Metronome structure
struct Metronome {
	metro_props_t metro_props;
	timer_props_t timer_props;
}typedef metronome_t;

// device paths
char *device_names[NUM_DEVICES] = {
	"/dev/local/metronome",
	"/dev/local/metronome-help"
};

// overridden attribute and open control block structures
typedef struct ioattr_t {
	iofunc_attr_t attr;
	int device;
} ioattr_t;

typedef struct metro_ocb{
	iofunc_ocb_t ocb;
	char buffer[50];
}metro_ocb_t;

/***************************************************************************************************************
 * Function declarations
 ***************************************************************************************************************/
void *metronome_thread();
int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb);
int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb);
int io_open(resmgr_context_t *ctp, io_open_t *msg, RESMGR_HANDLE_T *handle,void *extra);
struct itimerspec calculate_timer(metronome_t * metronome);
int search_data_table(metronome_t * metronome);
void stop_timer(struct itimerspec * itime, timer_t timer_id, metronome_t* metronome);
void pause_timer(struct itimerspec * itime, timer_t timer_id, message_t * msg);
void start_timer(struct itimerspec * itime, timer_t timer_id, metronome_t* metronome);
metro_ocb_t * metro_ocb_calloc(resmgr_context_t *ctp, IOFUNC_ATTR_T *mtattr);
void metro_ocb_free(IOFUNC_OCB_T *mocb);
void usage();


#endif /* SRC_METRONOME_H_ */
