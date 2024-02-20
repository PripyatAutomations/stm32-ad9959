/*
 * simple cli tool for controlling a chineze ad9959+stm32 DDS board over USB
 *
 * Sorry if it's messy, it was mostly thrown together on a monday morning!
 *
 * Build as such:
 * 	cc -ggdb -Wall -pedantic -o freqgen freqgen.c -lreadline -lev
 *
 * XXX: Implement -x to execute a one-off command from command line
 * XXX: Implement -l and -s for load and save (also load and save commands)
 * XXX: Deal with autoreconnecting
 */
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdarg.h>
#include <getopt.h>
#include <sys/select.h>

#define VERSION "2024-02-19.01"
#define	MAX_CHAN	4		// how many channels? ad9959 has 4...
#define BUFFER_SIZE 	512		// this should be plenty
#define	MAX_ARGS	5		// max arguments to a function...
#define	MAX_FREQ	200000000
#define	MIN_FREQ	1

struct cmds {
   char *name;
   int  min_args;
   int  max_args;
   void (*func)();
   char *msg;
};

// Defaults (cmdline config)
char *port = "/dev/ttyACM0";
int debug = 0;

// run-time state
char read_buffer[BUFFER_SIZE];
int buffer_index = 0;
int starting_up = 1;

char brd_ver[32];	// board version
int ref_clk = 25000000; // reference clock
int clk_mult = 1;	// clock multiplier

int curr_chan = 1;
struct ChannelState {
    int amp;		// amplitude
    int freq;		// frequency
    int phase;		// phase
    char mode[8];
    int sweep_start_amp,
        sweep_start_freq,
        sweep_end_amp,
        sweep_end_freq,
        sweep_step,
        sweep_time,
        sweep_active;
};
struct ChannelState chan_state[MAX_CHAN];

int open_serial_port(const char *port_name) {
    int fd = open(port_name, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    return fd;
}

void configure_serial_port(int fd) {
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }

    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_oflag &= ~OPOST;

    tty.c_cc[VTIME] = 0;
    tty.c_cc[VMIN] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
}

void uppercase(char *str) {
    while (*str) {
        *str = toupper((unsigned char)*str);
        str++;
    }
}

double convertPhaseToAngle(int value) {
    // Convert value to angle in the range 0-360
    return round(((double)value / 16383.0) * 360.0 * 10) / 10.0; // Round to nearest 0.1 degree
}

int convertAngleToPhase(double angle) {
    // Convert angle to the range 0-16383
    return (int)round((angle / 360.0) * 16383);
}

double convertAmplitudeToPower(int value) {
    // Convert value to power in the range 0-100%
    return ((double)value / 1023.0) * 100.0;
}

int convertPowerToAmplitude(double power) {
    // Convert power to the range 0-1023
    return (int)round((power / 100.0) * 1023);
}

double stringToDouble(const char *str) {
    // Convert string to double
    return strtod(str, NULL);
}

// yuck ;(
void c_power(); void c_chan(); void c_debug(); void c_freq();
void c_help(); void c_load(); void c_mode(); void c_mult();
void c_phase(); void c_quit(); void c_ref(); void c_reset();
void c_restore(); void c_save(); void c_starta(); void c_enda();
void c_startf(); void c_endf(); void c_version(); void c_step();
void c_time(); void c_sweep(); void c_info(); void c_sleep();

struct cmds cons_cmds[] = {
    { "chan", 	  0, 1, c_chan,		"Show/set channel [1-4]" },
    { "debug",    0, 1, c_debug,        "Show/set debug level [0-10]" },
    { "factory",  1, 1, c_restore, 	"Restore factory settings (must pass CONFIRM as arg!)" },
    { "freq",	  0, 1, c_freq,		"Show/set frequency [1-200,000,000] Hz" },
    { "help", 	  0, 0, c_help,		"This help message" },
    { "info",     0, 1, c_info,         "Show board information" },
    { "load",     0, 1, c_load,         "Load settings from stdin or file" },
    { "mode",	  0, 1, c_mode,		"Show/set mode [POINT|SWEEP|FSK2|FSK4|AM]" },
    { "mult",	  0, 1, c_mult,		"Show/set multiplier [1-20]" },
    { "phase",    0, 1, c_phase,        "Show/set phase [0.0-360.0] degrees" },
    { "power",    0, 1, c_power,        "Show/set power [0-1023] | [0-100%]" },
    { "quit",     0, 0, c_quit,         "Exit the program" },
    { "ref",	  0, 1, c_ref,		"Show/set refclk freq [10,000,000-125,000,000] Hz" },
    { "reset", 	  0, 0, c_reset,        "Reset the board" },
    { "save",     0, 1, c_save,         "Save the settings to stdout or file" },
    { "sleep",    1, 1, c_sleep,        "Sleep x ms" },
    { "enda",     0, 1, c_enda,         "Show/set sweep END amplitude [0-1023] | [0-100%]" },
    { "endf",     0, 1, c_endf,         "Show/set sweep END frequency [STARTFRE-200,000,000]" },
    { "starta",   0, 1, c_starta,       "Show/set sweep START amplitude [0-1023] | [0-100%]" },
    { "startf",   0, 1, c_startf,       "Show/set sweep START frequency [1-ENDFRE]" },
    { "step",     0, 1, c_step,         "Show/set sweep STEP interval [1-200,000,000] Hz" },
    { "sweep",    0, 1, c_sweep,        "Show/set sweep status [ON|OFF]" },
    { "time",     0, 1, c_time,         "Show/set sweep time [1-9999] ms" },
    { "ver", 	  0, 0, c_version,	"Show firmware version" },
    { (char *)NULL,     0, 0,  NULL,             NULL }
};

void send_command(int fd, const char *format, ...) {
    char buffer[BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, BUFFER_SIZE, format, args);
    va_end(args);
    
    if (debug) {
       printf("ser_send: %s\n", buffer);
    }

    write(fd, buffer, strlen(buffer));
    write(fd, "\r\n", 2);
}

void c_chan(int fd, char *argv[], int argc) {
    if (argc > 0) {
        curr_chan = atoi(argv[0]);
        if (debug) {
           printf("Selecting channel %i\n", curr_chan);
        }
        send_command(fd, "AT+CHANNEL+%i", curr_chan);
    }
    send_command(fd, "AT+CHANNEL");
}

void c_debug(int fd, char *argv[], int argc) {
    if (argc > 0) {
       int new_debug = atoi(argv[0]);
       printf("* Changing debug level from %d to %d\n", debug, new_debug);
       debug = new_debug;
    } else {
       printf("* Debug level: %d\n", debug);
    }
}

void c_enda(int fd, char *argv[], int argc) {
   if (argc > 0) {
      int new_amp = atoi(argv[0]);
      if (new_amp < 0 || new_amp > 1023) {
         printf("*** Invalid argument to enda: Value %d out of bounds [0-1023]\n", new_amp);
         return;
      }
      send_command(fd, "AT+ENDAMP+%d", new_amp);
   }
   send_command(fd, "AT+ENDAMP");
}

void c_endf(int fd, char *argv[], int argc) {
   if (argc > 0) {
      int new_freq = atoi(argv[0]);
      if (new_freq < MIN_FREQ || new_freq > MAX_FREQ) {
         printf("*** Invalid argument to endf: Value %d out of bounds [STARTFRE-200,000,000]\n", new_freq);
         return;
      }
      send_command(fd, "AT+ENDFRE+%d", new_freq);
   }
   send_command(fd, "AT+ENDFRE");
}

void c_freq(int fd, char *argv[], int argc) {
    if (argc > 0) {
        int new_freq = atoi(argv[0]);
        if (new_freq < MIN_FREQ || new_freq > MAX_FREQ) {
           printf("* Frequency %d is outside limits [%d - %d]\n", new_freq, MIN_FREQ, MAX_FREQ);
           return;
        }
        if (debug) {
           printf("Setting channel %d frequency to %s\n", curr_chan, argv[0]);
        }
        send_command(fd, "AT+FRE+%s", argv[0]);
    }
    send_command(fd, "AT+FRE");
}

void c_help(int fd, char *argv[], int argc) {
    struct cmds *c;
    printf("****\n");
    printf("AD9959 controller help:\n");
    printf("* name\tmin/max args\tDescription\n");
    int i = 0;
    while (i < 100) {
       c = &cons_cmds[i];

       // is this the last record?
       if (c->func == NULL && c->min_args == 0 && c->max_args == 0) {
          break;
       }
       printf("%s\t\t%d, %d\t%s\n", c->name, c->min_args, c->max_args, c->msg);
       i++;
    }
    printf("****\n");
}

void c_info(int fd, char *argv[], int argc) {
    c_version(fd, NULL, 0);
    c_ref(fd, NULL, 0);
    c_mult(fd, NULL, 0);
    c_chan(fd, NULL, 0);

    // clear starting flag
    starting_up = 0;
}

void c_load(int fd, char *argv[], int argc) {
   printf("Not yet implemented: load\n");
}

void c_mode(int fd, char *argv[], int argc) {
    if (argc > 0) {
        char *mode = strdup(argv[0]);
        if (!mode) {
           abort();
        }
        uppercase(mode);
        if (debug) {
           printf("Setting channel %d mode to %s\n", curr_chan, mode);
        }
        send_command(fd, "AT+MODE+%s", mode);
        free(mode);
    }
    send_command(fd, "AT+MODE");
}

void c_mult(int fd, char *argv[], int argc) {
    if (argc > 0) {
       int new_mult = atoi(argv[0]);
       printf("* Setting mult to %d Hz\n", new_mult);
       send_command(fd, "AT+MULT+%d", new_mult);
    }
    send_command(fd, "AT+MULT");
}

void c_phase(int fd, char *argv[], int argc) {
    if (argc > 0) {
       double new_angle = stringToDouble(argv[0]);
       int new_phase = convertAngleToPhase(new_angle);
       printf("- Chan %d changing phase to %.1f (%d)\n", curr_chan, new_angle, new_phase);
       send_command(fd, "AT+PHA+%d", new_phase);
    }
    send_command(fd, "AT+PHA");
}

void c_power(int fd, char *argv[], int argc) {
    // Min: 0, Max: 1023
    if (argc > 0) {
       int new_amp = atoi(argv[0]);
       char *percent_p = strchr(argv[0], '%');

       if (percent_p != NULL) {
          new_amp = convertPowerToAmplitude(new_amp);
       }
       if (new_amp < 0 || new_amp > 1023) {
           printf("*** Invalid value (%s) for GIVEN given: range 0-1023 or 0-100%%\n", argv[0]);
           return;
       }
       send_command(fd, "AT+AMP+%d", new_amp);
    }
    send_command(fd, "AT+AMP");
}

void c_quit(int fd, char *argv[], int argc) {
   printf("Goodbye!\n");
   exit(0);
}

void c_ref(int fd, char *argv[], int argc) {
    if (argc > 0) {
       int refclk = atoi(argv[0]);
       printf("* Setting refclk to %d Hz\n", refclk);
       send_command(fd, "AT+REF+%d", refclk);
    }
    send_command(fd, "AT+REF");
}

void c_reset(int fd, char *argv[], int argc) {
    printf("* Resetting board. Goodbye!\n");
    send_command(fd, "AT+RESET");
    exit(0);
}

void c_restore(int fd, char *argv[], int argc) {
    if (strcasecmp(argv[0], "CONFIRM") != 0) {
       printf("Please add CONFIRM to the command line, if sure!\n");
       return;
    }
    printf("* Sending factory reset to board. Goodbye!\n");
    send_command(fd, "AT+RESTORE");
    exit(0);
}

void c_save(int fd, char *argv[], int argc) {
   printf("Not yet implemented: save\n");
}

void c_sleep(int fd, char *argv[], int argc) {
   int sleepms = atoi(argv[0]);
   // limit to 1ms to 60 seconds
   if (sleepms <= 0 || sleepms > 60000) {
      printf("invalid sleep time %s limit [0-60000] ms\n", argv[0]);
      return;
   }

   printf("Sleep %d ms\n", sleepms);
   fflush(stdout);
   usleep(sleepms * 1000);
}

void c_starta(int fd, char *argv[], int argc) {
   if (argc > 0) {
      int new_amp = atoi(argv[0]);
      if (new_amp < 0 || new_amp > 1023) {
         printf("*** Invalid argument to starta: Value %d out of bounds [0-1023]\n", new_amp);
         return;
      }
      send_command(fd, "AT+STARTAMP+%d", new_amp);
   }
   send_command(fd, "AT+STARTAMP");
}

void c_startf(int fd, char *argv[], int argc) {
   if (argc > 0) {
      int new_freq = atoi(argv[0]);
      printf("new_freq: %d\n", new_freq);
      if (new_freq < 1 || new_freq > MAX_FREQ) {
         printf("*** Invalid argument to startf: Value %d out of bounds [1-200,000,000]\n", new_freq);
         return;
      }
      send_command(fd, "AT+STARTFRE+%d", new_freq);
   }
   send_command(fd, "AT+STARTFRE");
}

void c_step(int fd, char *argv[], int argc) {
    if (argc > 0) {
       int new_step = atoi(argv[0]);
       if (new_step < MIN_FREQ || new_step > MAX_FREQ) {
          printf("*** Invalid argument to step: Value %d out of bounds[1-200,000,000]\n", new_step);
          return;
       }
       send_command(fd, "AT+STEP+%d", new_step);
    }
   send_command(fd, "AT+STEP");
}

void c_sweep(int fd, char *argv[], int argc) {
   if (argc > 0) {
      int new_state;
      if (strncasecmp(argv[0], "OFF", 3) == 0) {
         new_state = 0;
      } else if (strncasecmp(argv[0], "ON", 2) == 0) {
         new_state = 1;
      } else {
         printf("*** Invalid argument %s to SWEEP\n", argv[0]);
      }
      if (chan_state[curr_chan-1].sweep_active != new_state) {
         if (debug) {
            printf("- Chan %d %sabling SWEEP\n", curr_chan, (new_state ? "en" : "dis"));
         }
         chan_state[curr_chan-1].sweep_active = new_state;
      }
      send_command(fd, "AT+SWEEP+%s", (new_state ? "ON" : "OFF"));
   }
   send_command(fd, "AT+SWEEP");
}

void c_time(int fd, char *argv[], int argc) {
    if (argc > 0) {
       int new_time = atoi(argv[0]);
       if (new_time < 1 || new_time > 9999) {
          printf("*** Invalid argument to time: Value %d out of bounds[1-9999]\n", new_time);
          return;
       }
       send_command(fd, "AT+TIME+%d", new_time);
    }
    send_command(fd, "AT+TIME");
}

void c_version(int fd, char *argv[], int argc) {
    send_command(fd, "AT+VERSION");
}

void process_line(int fd, const char *line) {
    if (debug) {
       printf("ser_read: %s\n", line);
    }

    // XXX: Deal with errors and tracking status from query_device_info commands
    if (strncmp(line, "OK", 2) == 0) {
       if (debug) {
          printf("OK!\n");
       }
    } else if (strcmp(line, "ERROR_DATA_OVER_RANGEM") == 0) {
       printf("*** Invalid argument data: Out of range! Last command was not successful!\n");
    // capture state messages
    } else if (strncmp(line, "+AMP=", 5) == 0) {
       int new_amp = atoi(line+5);
       chan_state[curr_chan-1].amp = new_amp;
       printf("- Chan %d amp: %d\n", curr_chan, new_amp);
    } else if (strncmp(line, "+CHANNEL=", 9) == 0) {
       curr_chan = atoi(line + 9);
       printf("* Chan %d selected\n", curr_chan);
       // query channel parameters to cause an update in struct
       c_mode(fd, NULL, 0);
    } else if (strncmp(line, "+ENDFRE=", 8) == 0) {
       chan_state[curr_chan-1].sweep_end_freq = atoi(line+8);
       printf("- Chan %d sweep end freq: %d\n", curr_chan, chan_state[curr_chan-1].sweep_end_freq);
    } else if (strncmp(line, "+FRE=", 5) == 0) {
       chan_state[curr_chan-1].freq = atoi(line + 5);
       printf("- Chan %d freq: %d\n", curr_chan, chan_state[curr_chan-1].freq);
    } else if (strncmp(line, "+MODE=", 6) == 0) {
       const char *new_mode = line + 6;
       size_t msz = sizeof(chan_state[curr_chan-1].mode);

       // zero buffer and save mode for this channel
       memset(chan_state[curr_chan-1].mode, 0, msz);
       snprintf(chan_state[curr_chan-1].mode, msz, "%s", new_mode);
       printf("- Chan %d mode: %s\n", curr_chan, chan_state[curr_chan-1].mode);

       if (strcasecmp(new_mode, "SWEEP") == 0) {
          // query sweep parameters
          c_starta(fd, NULL, 0);
          c_enda(fd, NULL, 0);
          c_startf(fd, NULL, 0);
          c_endf(fd, NULL, 0);
          c_time(fd, NULL, 0);
          c_step(fd, NULL, 0);
          c_sweep(fd, NULL, 0);
       } else if (strcasecmp(new_mode, "POINT") == 0) {
          c_freq(fd, NULL, 0);
          c_phase(fd, NULL, 0);
          c_power(fd, NULL, 0);
       } else if (strcasecmp(new_mode, "FSK2") == 0) {
          printf("*** Unsupported mode: %s\n", new_mode);
          return;
       } else if (strcasecmp(new_mode, "FSK4") == 0) {
          printf("*** Unsupported mode: %s\n", new_mode);
          return;
       } else if (strcasecmp(new_mode, "AM") == 0) {
          printf("*** Unsupported mode: %s\n", new_mode);
          return;
       }
    } else if (strncmp(line, "+MULT=", 6) == 0) {
       int tmp_mult = atoi(line+6);
       if (tmp_mult < 1 || tmp_mult > 20) {
          printf("*** Invalid mult argument data: Out of range! Last command was not succesful (MULT)!\n");
          return;
       }
       if (tmp_mult != clk_mult) {
          printf("* Multiplier: changed from %d to %d\n", clk_mult, tmp_mult);
          clk_mult = tmp_mult;
       } else {
          printf("* Multiplier: %d\n", clk_mult);
       }
    } else if (strncmp(line, "+PHA=", 5) == 0) {
       int new_phase = atoi(line + 5);
       double new_angle = convertPhaseToAngle(new_phase);
       chan_state[curr_chan-1].phase  = new_phase;
       printf("- Chan %d phase: %d (%.1f deg)\n", curr_chan, chan_state[curr_chan-1].phase, new_angle);
    } else if (strncmp(line, "+REF=", 5) == 0) {
       int tmp_refclk = atoi(line+5);
       if (tmp_refclk > 0) {
          if (tmp_refclk != ref_clk) {
             printf("* ClkRef: changed from %d to %d\n", ref_clk, tmp_refclk);
             ref_clk = tmp_refclk;
          } else {
             printf("* ClkRef: %d Hz\n", ref_clk);
          }
       }
    } else if (strncmp(line, "+ENDAMP=", 8) == 0) {
       int new_amp = atoi(line+8);
       if (new_amp != chan_state[curr_chan-1].sweep_end_amp) {
          printf("- Chan %d SWEEP End Amplitude: %d (was %d)\n", curr_chan, new_amp, chan_state[curr_chan-1].sweep_end_amp);
          chan_state[curr_chan-1].sweep_end_amp = new_amp;
       } else {
          printf("- Chan %d End Start Amplitude: %d\n", curr_chan, new_amp);
       }
    } else if (strncmp(line, "+STARTAMP=", 10) == 0) {
       int new_amp = atoi(line+10);
       if (new_amp != chan_state[curr_chan-1].sweep_start_amp) {
          printf("- Chan %d SWEEP Start Amplitude: %d (was %d)\n", curr_chan, new_amp, chan_state[curr_chan-1].sweep_start_amp);
          chan_state[curr_chan-1].sweep_start_amp = new_amp;
       } else {
          printf("- Chan %d SWEEP Start Amplitude: %d\n", curr_chan, new_amp);
       }
    } else if (strncmp(line, "+STARTFRE=", 10) == 0) {
       chan_state[curr_chan-1].sweep_start_freq = atoi(line+10);
       printf("- Chan %d sweep start freq: %d\n", curr_chan, chan_state[curr_chan-1].sweep_start_freq);
    } else if (strncmp(line, "+STEP=", 6) == 0) {
       chan_state[curr_chan-1].sweep_step = atoi(line+6);
       printf("- Chan %d sweep step: %d\n", curr_chan, chan_state[curr_chan-1].sweep_step);
    } else if (strncmp(line, "+SWEEP=", 7) == 0) {
       if (strncasecmp(line+7, "OFF", 3) == 0) {
          chan_state[curr_chan-1].sweep_active = 0;
          printf("- Chan %d sweep inactive\n", curr_chan);
       } else if (strncasecmp(line+7, "ON", 2) == 0) {
          chan_state[curr_chan-1].sweep_active = 1;
          printf("- Chan %d sweep ACTIVE\n", curr_chan);
       }
    } else if (strncmp(line, "+TIME=", 6) == 0) {
       chan_state[curr_chan-1].sweep_time = atoi(line+6);
       printf("- Chan %d sweep time: %d\n", curr_chan, chan_state[curr_chan-1].sweep_time);
    } else if (strncmp(line, "+VERSION=", 9) == 0) {
       memset(brd_ver, 0, sizeof(brd_ver));
       snprintf(brd_ver, sizeof(brd_ver), "%s", line + 9);
       printf("* Connected to board version %s\n", brd_ver);
    } else {
       printf("Unknown response (chan#%d): %s\n", curr_chan, line);
    }
}

void serial_read_cb(int fd) {
    static char read_buffer[BUFFER_SIZE];
    static int buffer_index = 0;
    char c;
    ssize_t nbytes = read(fd, &c, 1);
    if (nbytes > 0) {
        if (c == '\r' || c == '\n') {
            if (buffer_index > 0) {
                read_buffer[buffer_index] = '\0'; 	// Null-terminate the string
                process_line(fd, read_buffer);
                buffer_index = 0;
            }
        } else {
            if (buffer_index < BUFFER_SIZE - 1) {
                read_buffer[buffer_index++] = c;
            } else {
                printf("overflow!\n");
            }
        }
    }
}

void handle_command(int fd, const char *input) {
    char *command = strtok((char *)input, " \t\r\n");

    // no command given, skip so it doesn't end up in history if we ever implement it...
    if (command == NULL) {
        return;
    }

    // iterate over all the known commands
    int i = 0;
    while (cons_cmds[i].name != NULL) {
        if (strcasecmp(command, cons_cmds[i].name) == 0) {
            char *args[MAX_ARGS];
            int num_args = 0;
            char *arg = strtok(NULL, " \t\r\n");

            while (arg != NULL && num_args < MAX_ARGS) {
                args[num_args++] = arg;
                arg = strtok(NULL, " \t\r\n");
            }

            if (num_args < cons_cmds[i].min_args || num_args > cons_cmds[i].max_args) {
                printf("Usage: %s %s\n", cons_cmds[i].name, cons_cmds[i].msg);
            } else {
                cons_cmds[i].func(fd, args, num_args);
            }
            return;
        }
        i++;
    }

    printf("Unknown command: %s\n", command);
}

void show_help(int argc, char **argv) {
    printf("Usage: %s [option] - Control AD9959+stm32 DDS VFO board from ch*na\n", argv[0]);
    printf("\t-h\t\tThis help message\n");
    printf("\t-p\t\tSerial port path\n");
    printf("\t-d\t\tDebug level\n");
}

int main(int argc, char **argv) {
    int opt;

    struct option long_options[] = {
        {"port", required_argument, NULL, 'p'},
        {"debug", optional_argument, NULL, 'd'},
        {"help", no_argument, NULL, 'h'},
        {"load", no_argument, NULL, 'l'},
        {"save", no_argument, NULL, 's'},
        {"exec", no_argument, NULL, 'x'},
        {NULL, 0, NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "p:d::hl:sx", long_options, NULL)) != -1) {
        switch (opt) {
            case 'p':
                port = optarg;
                break;
            case 'd':
                if (optarg != NULL) {
                    debug = atoi(optarg);
                    if (debug < 1) {
                        debug = 1; // Set debug to 1 if an invalid value is provided
                    }
                } else {
                    debug = 1; // Default debug level if no value provided
                }
                break;
            case 'h':
                show_help(argc, argv);
                exit(EXIT_SUCCESS);
            case 'l':
                // Call c_load function
                printf("Calling c_load function\n");
                break;
            case 's':
                // Call c_save function
                printf("Calling c_save function\n");
                break;
            case 'x':
                // Call c_exec function
                printf("Calling c_exec function\n");
                // exec_cmd(argc, argv);
                exit(0);
                break;
            default:
                // Invalid option
                show_help(argc, argv);
                exit(EXIT_FAILURE);
        }
    }

    // zero out channel state storage
    memset(chan_state, 0, sizeof(struct ChannelState) * MAX_CHAN);

    int serial_fd = open_serial_port(port);
    configure_serial_port(serial_fd);

    printf("Chineze ad9959 DDS board control widget v%s starting (debug: %d)!\n", VERSION, debug);
    printf("Serial port %s connected on fd %d. Type 'help' for commands or press Ctrl+C to exit.\n", port, serial_fd);

    c_info(serial_fd, NULL, 0);
    fd_set rfds;
    struct timeval tv;

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(serial_fd, &rfds);

        tv.tv_sec = 0;
        tv.tv_usec = 10000; // 10ms

        if (select(serial_fd + 1, &rfds, NULL, NULL, &tv) > 0) {
            if (FD_ISSET(STDIN_FILENO, &rfds)) {
                char buf[BUFFER_SIZE];
                if (fgets(buf, sizeof(buf), stdin) != NULL) {
                    handle_command(serial_fd, buf);
                }
            }
            if (FD_ISSET(serial_fd, &rfds)) {
                serial_read_cb(serial_fd);
            }
        }
    }

    close(serial_fd);

    return 0;
}
