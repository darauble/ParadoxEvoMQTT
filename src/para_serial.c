#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>


#include "log.h"
#include "para_serial.h"
#include "endpoints.h"
#include "zmq_helpers.h"

static int fd;

void *serial_thread(void *);

pthread_t start_para_serial(char *device, void *context)
{
    fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);

    if (fd < 0) {
        log_error("SERIAL: Could not open device handler: %d\n", fd);
        return (pthread_t) NULL;
    }

    struct termios tio = {
		.c_cflag = PARA_SERIAL_SPEED | CS8 | CLOCAL | CREAD,
		.c_iflag = 0,
		.c_oflag = 0,
		.c_lflag = NOFLSH,
		.c_cc = {0},
	};

	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &tio);

    // Start threads
    pthread_t thread;

    pthread_create(&thread, NULL, serial_thread, context);

    return thread;
}

void *serial_thread(void *context)
{
    __label__ EXIT_SERIAL_THREAD;
    // TODO: Create writing thread
    log_info("SERIAL: starting thread...\n");

    void *command_receiver = NULL;
    void *kill_subscriber = NULL;
    void *serial_publisher = NULL;
    int rc;

    /******* Initialize sockets **********/
    if ((rc = z_start_endpoint(context, &command_receiver, ZMQ_PULL, EPT_SERIAL_WRITE)) != 0) {
        log_error("SERIAL: cannot start command receiver: %d, exiting.\n", rc);
        goto EXIT_SERIAL_THREAD;
    }

    // Lamely give time for ZMQ PUB somewhere to initialize.
    sleep(1);

    if ((rc = z_connect_endpoint(context, &serial_publisher, ZMQ_PUSH, EPT_SERIAL_READ)) != 0) {
        log_error("SERIAL: cannot start serial publisher: %d, exiting.\n", rc);
        goto EXIT_SERIAL_THREAD;
    }
    
    if ((rc = z_connect_endpoint(context, &kill_subscriber, ZMQ_SUB, EPT_KILL)) != 0) {
        log_error("SERIAL: cannot subscribe to kill endpoint: %d, exiting.\n", rc);
        goto EXIT_SERIAL_THREAD;
    }//*/

    zmq_setsockopt(kill_subscriber, ZMQ_SUBSCRIBE, "", 0);
    /******* END of Initialize sockets **********/

    char buffer[PARA_SERIAL_BUFF_LEN];
    memset(buffer, 0, PARA_SERIAL_BUFF_LEN);
    int bufpos = 0;

    zmq_pollitem_t items[] = {
        { kill_subscriber, 0, ZMQ_POLLIN, 0 },
        { NULL, fd, ZMQ_POLLIN | ZMQ_POLLERR, 0 },
        { command_receiver, 0, ZMQ_POLLIN, 0 },
    };

    log_info("SERIAL: thread ready!\n");

    while(1) {
        zmq_poll(items, 3, -1);

        if (items[0].revents & ZMQ_POLLIN) {
            z_drop_message(kill_subscriber);
            log_info("SERIAL: received KILL, exitting\n");
            break;
        } else if (items[1].revents & ZMQ_POLLIN) {
            // Bytes in serial to read?
            ssize_t br = read(fd, &buffer[bufpos], 1);

            while (br > 0) {
                if (buffer[bufpos] == PARA_SERIAL_EOL) {
                    buffer[bufpos] = 0;
                    log_verbose("SERIAL: [%s]\n", buffer);
                    // TODO: send out
                    zmq_msg_t message;

                    zmq_msg_init_size (&message, bufpos);
                    memcpy(zmq_msg_data(&message), buffer, bufpos);
                    zmq_msg_send (&message, serial_publisher, 0);
                    zmq_msg_close (&message);

                    // Cleanup and read again
                    memset(buffer, 0, PARA_SERIAL_BUFF_LEN);
                    bufpos = 0;
                } else if (bufpos < PARA_SERIAL_BUFF_LEN) {
                    bufpos++;
                } else {
                    log_debug("SERIAL: reached end of buffer, cleaning for next round.\n");
                    memset(buffer, 0, PARA_SERIAL_BUFF_LEN);
                    bufpos = 0;
                }

                br = read(fd, &buffer[bufpos], 1);
            }/* else if (br <= 0) {
                log_error("SERIAL: error reading from device %ld, exiting.\n", br);
                break;
            }*/
        } else if (items[2].revents & ZMQ_POLLIN) {
            // log_info("SERIAL: receiving command to send to EVO...\n");
            zmq_msg_t message;
            
            zmq_msg_init (&message);
            int size = zmq_msg_recv (&message, command_receiver, 0);

            if (size > 0) {
                char *output = malloc(size + 1);
                
                memcpy (output, zmq_msg_data (&message), size);
                output[size] = 0;
                
                log_debug("SERIAL: sending the command out: %s...\n", output);
                output[size] = PARA_SERIAL_EOL;

                ssize_t wrc = write(fd, output, size + 1);
                
                if (wrc != size + 1) {
                    log_debug("\nSERIAL: wrote less bytes than expected: %ld!\n", wrc);
                }
                //tcflush(fd, TCIOFLUSH);
                
                //log_info("%ld bytes written\n", wrc);

                free(output);
                log_debug("done\n");
            }

            if (size >= 0) {
                zmq_msg_close (&message);
            }
        } else {
            log_info("SERIAL: POLLERR?..\n");
        }
    }//*/

EXIT_SERIAL_THREAD:
    if (command_receiver) {
        zmq_close(command_receiver);
    }
    
    if (serial_publisher) {
        zmq_close(serial_publisher);
    }

    if (kill_subscriber) {
        zmq_close(kill_subscriber);
    }

    close(fd);
    
    return NULL;
}
