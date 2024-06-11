
/*
 *
 *    Copyright (c) 2002, Smart Link Ltd.
 *    All rights reserved.
 *
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the following conditions
 *    are met:
 *
 *        1. Redistributions of source code must retain the above copyright
 *           notice, this list of conditions and the following disclaimer.
 *        2. Redistributions in binary form must reproduce the above
 *           copyright notice, this list of conditions and the following
 *           disclaimer in the documentation and/or other materials provided
 *           with the distribution.
 *        3. Neither the name of the Smart Link Ltd. nor the names of its
 *           contributors may be used to endorse or promote products derived
 *           from this software without specific prior written permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * dodgy hackery of the modem_test program to make a fake inbound fax modem
 * charlie camilleri '24
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <time.h>
#include <dirent.h>
#include <uuid/uuid.h>
#include <postgresql/libpq-fe.h>
#include <sys/prctl.h>

#include <modem.h>
#include <modem_debug.h>

#include "asterisk/audiosocket.h"
#include "image-converter.h"

#define INFO(fmt,args...) fprintf(stderr, fmt , ##args );
#define ERR(fmt,args...) fprintf(stderr, "error: " fmt , ##args );

#define DBG(fmt,args...) if(modem_debug_level) \
                             fprintf(stderr, "main: " fmt , ##args );


#define SIMULATE_RING 1 // hehe time for the nasty hacks

extern unsigned int modem_debug_level;

/* modem init externals : FIXME remove it */
extern int  dp_dummy_init(void);
extern void dp_dummy_exit(void);
extern int  dp_sinus_init(void);
extern void dp_sinus_exit(void);
extern int  prop_dp_init(void);
extern void prop_dp_exit(void);
extern int datafile_load_info(char *name,struct dsp_info *info);
extern int datafile_save_info(char *name,struct dsp_info *info);


/* global config data */
extern unsigned modem_debug_logging;


struct charliefax {
	struct modem *modem;
	struct charliefax *link;
	int in,out;
	unsigned int delay;
	unsigned int started;
	unsigned pty_closed;
	unsigned close_count;
};


#define CLOSE_COUNT_MAX 100

/* static data */
static char inbuf[4096];
static char outbuf[4096];


/* 'driver' simulation */

static int charliefax_modem_start (struct modem *m)
{
	struct charliefax *t = m->dev_data;
	DBG("charliefax_start...\n");
	t->delay = 256;
	t->started = 1;
	memset(outbuf,0,t->delay);
	write(t->out,outbuf,t->delay);
	return 0;
}

static int charliefax_modem_stop (struct modem *m)
{
	struct charliefax *t = m->dev_data;
	DBG("charliefax_stop...\n");
	t->started = 0;
	t->delay = 0;
	return 0;
}

static int charliefax_modem_ioctl(struct modem *m, unsigned int cmd, unsigned long arg)
{
	struct charliefax *t = m->dev_data;
	DBG("charliefax_ioctl: cmd %x, arg %lx...\n",cmd,arg);
        switch (cmd) {
        case MDMCTL_CAPABILITIES:
                return -1;
        case MDMCTL_HOOKSTATE:
		return 0;
        case MDMCTL_SPEED:
                return 0;
        case MDMCTL_GETFMTS:
        case MDMCTL_SETFMT:
                return 0;
        case MDMCTL_SETFRAGMENT:
                return 0;
        case MDMCTL_SPEAKERVOL:
		return 0;
        case MDMCTL_CODECTYPE:
                return CODEC_UNKNOWN;
        case MDMCTL_IODELAY:
		return t->delay/2;
        default:
                break;
        }
	return -2;
}



struct modem_driver charliefax_driver = {
        .name = "charliefax driver",
        .start = charliefax_modem_start,
        .stop = charliefax_modem_stop,
        .ioctl = charliefax_modem_ioctl,
};

static int charliefax_modem_run(struct charliefax *modems, sig_atomic_t *ipc)
{
	struct timeval tmo;
	fd_set rset,eset;
	struct termios termios;
	struct charliefax *t;
	void *in;
	int max_fd = 0;
	int count, ret;

#ifdef SIMULATE_RING
#define RING_HZ 20
#define RING_ON  1*RING_HZ
#define RING_OFF 4*RING_HZ
	unsigned rcount = RING_ON;
#endif


	while(ipc[0]) {

		for( t = modems ; t->modem ; t++ )
			if(t->modem->event)
				modem_event(t->modem);
#ifdef SIMULATE_RING
		tmo.tv_sec = 0;
		tmo.tv_usec= 1000000/RING_HZ;
#else
                tmo.tv_sec = 1;
                tmo.tv_usec= 0;
#endif
        FD_ZERO(&rset);
		FD_ZERO(&eset);
		max_fd = 0;

		for( t = modems ; t->modem ; t++ ) {
			FD_SET(t->in,&rset);
			FD_SET(t->in,&eset);
			if(t->in > max_fd)
				max_fd = t->in;
			if( t->pty_closed && t->close_count ) {
				if ( !t->started ||
				     t->close_count++ > CLOSE_COUNT_MAX )
					t->close_count = 0;
			}
			else if (t->modem->xmit.size - t->modem->xmit.count > 0) {
				FD_SET(t->modem->pty,&rset);
				if(t->modem->pty > max_fd)
					max_fd = t->modem->pty;
			}
		}

		ret = select(max_fd + 1,&rset,NULL,&eset,&tmo);

		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			}
			ERR("select: %s\n",strerror(errno));
			return ret;
		}

		//DBG("select = %d\n",ret);
		if (ret == 0) {
#ifdef SIMULATE_RING 
			if(!modems->modem->started) {
				rcount++;
				if (rcount <= RING_ON) {
					modem_ring(modems->modem);
				} else if (rcount > RING_OFF)
					rcount = 0;
			} else {
				ipc[1] = 1;
			}
#endif
			continue;
		}

		for( t = modems ; t->modem ; t++ ) {
			if(FD_ISSET(t->in,&eset)) {
				DBG("dev exception...\n");
			}
		}

		for( t = modems ; t->modem ; t++ ) {
			if(FD_ISSET(t->in,&rset)) {
				//DBG("dev read...\n");
				count = read(t->in,inbuf,sizeof(inbuf)/2);
				if(count < 0) {
					ERR("dev read: %s\n",strerror(errno));
					return -1;
				}
				else if (count == 0) {
					DBG("dev read = 0\n");
					continue;
				}
				in = inbuf;
				if(t->modem->update_delay < 0) {
					if ( -t->modem->update_delay >= count/2) {
						DBG("change delay -%d...\n", count/2);
						t->delay -= count;
						t->modem->update_delay += count/2;
						continue;
					}
					DBG("change delay %d...\n", t->modem->update_delay);
					in -= t->modem->update_delay*2;
					count += t->modem->update_delay*2;
					t->delay += t->modem->update_delay*2;
					t->modem->update_delay = 0;
				}
				if(t->started) {
					modem_process(t->modem,in,outbuf,count>>1);
				}
				else {
					memset(outbuf,0,count);
					/* ring here */
				}
				count = write(t->out,outbuf,count);
				if(count < 0) {
					ERR("dev write: %s\n",strerror(errno));
					return -1;
				}
				else if (count == 0) {
					DBG("dev write = 0\n");
				}

				if(t->modem->update_delay > 0) {
					DBG("change delay %d...\n", t->modem->update_delay);
					memset(outbuf, 0, t->modem->update_delay*2);
					count = write(t->out,outbuf,t->modem->update_delay*2);
					if(count < 0) {
						ERR("dev write: %s\n",strerror(errno));
						return -1;
					}
					t->delay += t->modem->update_delay*2;
					t->modem->update_delay = 0;
				}
			}
		}

		for( t = modems ; t->modem ; t++ ) {
			int pty = t->modem->pty;
			if(FD_ISSET(pty,&rset)) {
				//DBG("pty read...\n");
				/* check termios */
				tcgetattr(pty,&termios);
				if(memcmp(&termios,&t->modem->termios,
					  sizeof(termios))) {
					DBG("termios changed.\n");
					modem_update_termios(t->modem, &termios);
				}
				/* read data */
				count = t->modem->xmit.size - t->modem->xmit.count;
				if(count == 0)
					continue;
				if (count > sizeof(inbuf))
					count = sizeof(inbuf);
				count = read(pty,inbuf,count);
				if(count < 0) {
					if(errno == EAGAIN) {
						DBG("pty read, errno = EAGAIN\n");
						continue;
					}
					if(errno == EIO) {
						if(!t->pty_closed) {
							DBG("pty closed.\n");
							t->pty_closed = 1;
							if(termios.c_cflag&HUPCL)
								modem_hangup(t->modem);
						}
						t->close_count = 1;
						//DBG("pty read, errno=EIO\n");
						continue;
					}
					else
						ERR("pty read: %s\n",
						    strerror(errno));
					return -1;
				}
				else if (count == 0) {
					DBG("pty read = 0\n");
				}
				t->pty_closed = 0;
				//DBG("pty read %d\n",count);
				count = modem_write(t->modem,inbuf,count);
				if(count < 0) {
					DBG("modem_write failed.\n");
					return -1;
				}
			}
		}
	}

	return 0;
}


static int charliefax_modem_init(struct charliefax *t, const char *name, int in, int out)
{
	struct termios termios;
	char *pty_name;
	int pty;
	int ret;
	
	memset(t,0,sizeof(*t));

	t->in  = in;
	t->out = out;

        pty  = getpt();
        if (pty < 0 || grantpt(pty) < 0 || unlockpt(pty) < 0) {
                ERR("getpt: %s\n",strerror(errno));
                exit(-1);
        }

        ret = tcgetattr(pty, &termios);
        /* non canonical raw tty */
        cfmakeraw(&termios);
        cfsetispeed(&termios, B115200);
        cfsetospeed(&termios, B115200);

        ret = tcsetattr(pty, TCSANOW, &termios);
        if (ret) {
                ERR("tcsetattr: %s\n",strerror(errno));
                exit(-1);
        }

	fcntl(pty,F_SETFL,O_NONBLOCK);

	pty_name = ptsname(pty);

	t->modem = modem_create(&charliefax_driver,name);
	if(!t->modem) {
		return -1;
	}
	t->modem->name = name;
	t->modem->pty = pty;
	//t->modem->dev = dev;
	t->modem->dev_name = name;
	t->modem->pty_name = pty_name;

	//t->modem->sregs[0] = 1; // AA after one ring (as the modem only exists during this phone call)
	//t->modem->started = 1;

	// datafile_load_info(basename(dev_name),&t->modem->dsp_info);
	modem_update_termios(t->modem,&termios);
	t->modem->dev_data = t;

	DBG("created %s: %s\n",t->modem->name,t->modem->pty_name);

	return 0;
}

static void charliefax_modem_free(struct charliefax *t)
{
	int pty = t->modem->pty;
	modem_delete(t->modem);
	close(pty);
}


int charliefax_modem(const int in_fd, const int out_fd, const char* tdir, sig_atomic_t* ipc, const char* config_efax, const char* csid)
{
	struct charliefax *ma;
	struct charliefax modems[2] = {};
	//int pipe1[2];
	int ret = 0;

	modem_debug_init("test");

	memset(modems,0,sizeof(modems));

	dp_dummy_init();
	dp_sinus_init();
	prop_dp_init();
	modem_timer_init();

	ma = &modems[0];

	char* name;
	if (asprintf(&name, "modem-%u", getpid()) < 0) {
		perror("asprintf()");
		dp_dummy_exit();
		dp_sinus_exit();
		prop_dp_exit();
		return errno;
	}

	charliefax_modem_init(ma,name,in_fd, out_fd);

	printf("----> EFAX TEMP DIR = %s\n", tdir);

	pid_t efax_pid = fork();
	if (efax_pid < 0) {
		perror("fork()");
		ipc[0] = 0;
		charliefax_modem_free(ma);
		dp_dummy_exit();
		dp_sinus_exit();
		prop_dp_exit();
		exit(EXIT_FAILURE);
	}

	if (efax_pid == 0) {
		mkdir(tdir, S_IWUSR | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH); // mode 755
		chdir(tdir);
		if (execl(config_efax, config_efax, "-o","1","-d", ma->modem->pty_name, "-l", csid,"-r","fax", NULL) < 0) {
			perror("execl()");
			ipc[0] = 0;
			charliefax_modem_free(ma);
			dp_dummy_exit();
			dp_sinus_exit();
			prop_dp_exit();
			exit(EXIT_FAILURE);
		}
	}

	ipc[2] = efax_pid;
	printf("efax started, pid = %u\n", ipc[2]);

	ma->link = NULL;
	//mb->link = ma;

	/* main loop here */
	ret = charliefax_modem_run(modems, ipc);

	int efax_status;
	printf("waiting for efax process to die..\n");
	kill(efax_pid, SIGTERM);
	if (waitpid(efax_pid, &efax_status, 0) < 0)
		if (errno != ECHILD)
			perror("waitpid()");

	printf("efax[%u] exited with code %d\n", efax_pid, efax_status);
	ipc[2] = WEXITSTATUS(efax_status);

	charliefax_modem_free(ma);

	dp_dummy_exit();
	dp_sinus_exit();
	prop_dp_exit();

	close(in_fd);
	close(out_fd);

	return ret;
}

#define PORT 9092
#define INTERP 5
#define TCP_BUFSIZE 512*INTERP // we are on modern machines with loads of ram so this is fine! :tm:

int conn_handler(const int clifd, char* db_conninfo, const char* config_efax, const char* csid, const char* inbound_path); // these are not const as we erase them post-fork() for security. (modem driver has executable stack, we must be careful!)
int main(int argc, char** argv) {
	printf("charliefax (C) charlie camilleri '24\n");

	if (argc != 2) {
		fprintf(stderr, "usage: %s <config file>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	FILE* f = fopen(argv[1], "r");
	if (f == NULL) {
		perror("fopen()");
		exit(errno);
	}

	fseek(f, 0, SEEK_END);
	size_t confsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* config_buf = (char*)malloc(confsize);
	if (fread(config_buf, 1, confsize, f) != confsize) {
		perror("fread()");
		exit(errno);
	} 
	fclose(f);

	char *config_postgres = NULL;
	char *config_efax = NULL;
	char *config_host = NULL;
	char *config_port = NULL;
	char *config_csid = NULL;
	char *config_conns = NULL;
	char *config_inbound_path = NULL;

	char *conftok_outer, *conftok_inner;
	char *tok = strtok_r(config_buf, "\n", &conftok_outer);
	while(tok != NULL) {
		char* line = strdup(tok);
		char* key = strtok_r(line, "=", &conftok_inner);
		char* value = conftok_inner;
		
		if (strcmp(key, "PostgresDB") == 0) {
			if (strlen(value)>2 && value[0] == '"') value++;
			config_postgres = strdup(value);
		} else if (strcmp(key, "efax") == 0)
			config_efax = strdup(value);
		else if (strcmp(key, "ListenPort") == 0)
			config_port = strdup(value);
		else if (strcmp(key, "ListenHost") == 0)
			config_host = strdup(value);
		else if (strcmp(key, "FaxCSID") == 0)
			config_csid = strdup(value);
		else if (strcmp(key, "MaxConnections") == 0)
			config_conns = strdup(value);
		else if (strcmp(key, "InboundFaxPath") == 0)
			config_inbound_path = strdup(value);
		else
			fprintf(stderr, "warning: unknown config key %s\n", key);

		free(line);
		tok = strtok_r(NULL, " \n", &conftok_outer);
	}

	if (config_efax == NULL || config_postgres == NULL || config_host == NULL || config_port == NULL || config_csid == NULL || config_conns == NULL || config_inbound_path == NULL) {
		fprintf(stderr, "Missing configuration options!\n");
		exit(EXIT_FAILURE);
	}

	if (config_postgres[strlen(config_postgres)-1] == '"') config_postgres[strlen(config_postgres)-1] = '\0';

	int maxconns = strtoul(config_conns, NULL, 10);
	if (errno == ERANGE || maxconns == 0) {
		fprintf(stderr, "Invalid MaxConnections\n");
		exit(EXIT_FAILURE);
	}
	free(config_conns);

	int port = strtoul(config_port, NULL, 10);
	if (errno == ERANGE || port == 0 || port >= 65535) {
		fprintf(stderr, "Invalid ListenPort\n");
		exit(EXIT_FAILURE);
	}
	free(config_port);

	printf("read config OK\n");
	free(config_buf);

	//

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket()");
        exit(errno);
    }

    int sockoptval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&sockoptval, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(sockfd);
        exit(errno);
    }

    struct sockaddr_in serv, cli;
    
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(config_host);
    serv.sin_port = htons(port);
	

    if (bind(sockfd, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("bind()");
        close(sockfd);
        exit(errno);
    }

    if (listen(sockfd, maxconns) < 0) {
        perror("listen()");
        close(sockfd);
        exit(errno);
    }
	printf("Listening on %s:%d [maxconns=%d]\n", config_host, port, maxconns);

	free(config_host);

	mkdir(config_inbound_path, 0755);
	
	unsigned int clen = sizeof(cli);
    for(;;) {
        fprintf(stderr,"ready to accept connections\n");
        int clifd = accept(sockfd, (struct sockaddr*)&cli, &clen);
        if (clifd < 0) {
            perror("accept()");
            close(sockfd);
            exit(errno);
        }

		pid_t child = fork();
		if (child < 0) {
			perror("fork()"); // who knows, we may be out of resources! Might as well cleanly fail as we don't want a DoS. (having said that, this server should never be publicly exposed.)
			close(clifd);
			continue;
		}

		if (child == 0) {
			if (conn_handler(clifd, config_postgres, config_efax, config_csid, config_inbound_path) != 0) {
				printf("conn_handler error\n");
			}
			return 0;
		}

		printf("connection from %s spawned child %u\n", inet_ntoa(cli.sin_addr), child);
		close(clifd);
		int reap_state;
		pid_t termed;
		while ((termed = waitpid(-1, &reap_state, WNOHANG)) > 0)
			if (WIFEXITED(reap_state))
				printf("child process %u exited with code %d\n", termed, WEXITSTATUS(reap_state));
    }

	free(config_efax);
	free(config_postgres);
	free(config_csid);

    close(sockfd);

    exit(EXIT_SUCCESS);
}

int conn_handler(const int clifd, char* db_conninfo, const char* config_efax, const char* csid, const char* inbound_path){
    char buf[TCP_BUFSIZE];
	short *buf_shrt = (short*)(buf + sizeof(struct asterisk_audiosock_hdr));
	char raw_samps[TCP_BUFSIZE];
	short *raw_samps_shrt = (short*)raw_samps;
    size_t n;
    char connected = 0;
	char uuid[37];

	int asterisk_to_modem_pipe[2];
	int modem_to_asterisk_pipe[2];

	if (pipe2(asterisk_to_modem_pipe, O_CLOEXEC) < 0 || pipe2(modem_to_asterisk_pipe, O_CLOEXEC) < 0) {
		perror("pipe()");
		return errno;
	}

	sig_atomic_t *modem_ipc = (sig_atomic_t*)mmap(NULL, sizeof(sig_atomic_t) * 3, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	modem_ipc[0] = 1; // kill modem
	modem_ipc[1] = 0; // answered
	modem_ipc[2] = 0; // handler exit status

	char fax_timestamp[128];
	time_t ts = time(NULL);
	struct tm *tm = gmtime(&ts);
	strftime(fax_timestamp, sizeof(fax_timestamp), "%Y%m%d_%H%M%S", tm);

	char fax_tdir[256];
	snprintf(fax_tdir, sizeof(fax_tdir), "%s/_fax_tmp-%s-%u", inbound_path, fax_timestamp, getpid());

	pid_t modem_child = fork();
	if (modem_child < 0) {
		perror("fork()");
		return errno;
	}

	if (modem_child == 0) {
		memset(db_conninfo, 0, strlen(db_conninfo) + 1);
		db_conninfo = NULL; // avoid leaking the DB password into the child as the modem driver may well have issues (and it has an RWX stack!)

		printf("modem started with pid %u\n", getpid());
		extern void modem_cmdline(int argc, char *argv[]);
		modem_debug_level = 0;
		char *fake_argv[2] = {"modem", NULL};
		modem_cmdline(1,fake_argv);

		int ret = charliefax_modem(asterisk_to_modem_pipe[0], modem_to_asterisk_pipe[1], fax_tdir, modem_ipc, config_efax, csid);
		printf("modem exited with code %d\n", ret);
		modem_ipc[0] = 0;
		exit(ret);
		return ret;
	}

	connected=1;

	fd_set sockets_set;
	int maxfd = clifd;
	if (modem_to_asterisk_pipe[0] > maxfd) maxfd = modem_to_asterisk_pipe[0];

	int selret;
	struct timeval tv_sel;

	int got_uuid = 0;

	while (connected==1) { // rx loop
		tv_sel = (struct timeval){.tv_sec = 0, .tv_usec = 10000};

		FD_ZERO(&sockets_set);
		FD_SET(clifd, &sockets_set);
		FD_SET(modem_to_asterisk_pipe[0], &sockets_set);

		if ((selret = select(maxfd + 2, &sockets_set, NULL, NULL, &tv_sel)) < 0) {
			perror("select()");
			goto err_exit;
		}

		int modem_child_status;
		if (waitpid(modem_child, &modem_child_status, WNOHANG) < 0) {
			perror("waitpid()");
			goto err_exit;
		}

		if (modem_ipc[0] == 0) {
			printf("error: modem process terminated\n");
			connected=0;
			break;
		}

		if (selret == 0) continue;

		if (FD_ISSET(clifd, &sockets_set)) {
			n = recv(clifd, buf, sizeof(struct asterisk_audiosock_hdr), MSG_WAITALL);
			if (n < 0) {
				perror("read()");
				goto err_exit;
			}

			if (n != sizeof(struct asterisk_audiosock_hdr)) {
				printf("remote closed connection\n");
				connected=0;
				break;
			}

			struct asterisk_audiosock_hdr *aah = (struct asterisk_audiosock_hdr*)buf;
			aah->len = ntohs(aah->len);

			if (aah->type != AUDIOSOCKET_TYPE_AUDIO) fprintf(stderr,"rx frame type=0x%02X, len=%u\n", aah->type, aah->len);

			if (aah->len > 0) {
				n = recv(clifd, buf + sizeof(struct asterisk_audiosock_hdr), aah->len, MSG_WAITALL); // now read payload
				if (n < 0) {
					perror("read()");
					goto err_exit;
				}
			}

			switch (aah->type) {
				case AUDIOSOCKET_TYPE_TERMINATE:
					fprintf(stderr,"remote terminated connection\n");
					close(clifd);
					connected=0;
					break;
				
				case AUDIOSOCKET_TYPE_ERROR:
					fprintf(stderr,"remote error, terminating\n");
					close(clifd);
					connected=0;
					break;
				
				case AUDIOSOCKET_TYPE_UUID:
					uuid_unparse_lower((unsigned char*)(buf + sizeof(struct asterisk_audiosock_hdr)), uuid);
					fprintf(stderr,"uuid received: %s\n", uuid);
					got_uuid = 1;
					break;

				case AUDIOSOCKET_TYPE_AUDIO: {
					if (modem_ipc[1]) {
						memset(raw_samps_shrt, 0, aah->len/INTERP);
						for (int i=0;i<aah->len/(2*INTERP);i++) {
							int b = 0;
							for (int j=0;j<INTERP;j++)
								b += buf_shrt[(i*INTERP) + j];
							raw_samps_shrt[i] = (b/5)&0xFFFF;
						}

						if (write(asterisk_to_modem_pipe[1], raw_samps, aah->len/INTERP) != aah->len/INTERP) {
							perror("write()");
							goto err_exit;
						}
					}

					break;
				}
			}
		}
		
		if (FD_ISSET(modem_to_asterisk_pipe[0], &sockets_set)) {
			n = read(modem_to_asterisk_pipe[0], raw_samps, 1600/INTERP);
			if (n < 0) {
				perror("read()");
				goto err_exit;
			}

			memset(buf + sizeof(struct asterisk_audiosock_hdr), 0, n*INTERP);
			for (int i=0;i<(n*INTERP)/2;i+=INTERP)
				buf_shrt[i] = raw_samps_shrt[i/INTERP];

			if (got_uuid == 1) {
				struct asterisk_audiosock_hdr *aah = (struct asterisk_audiosock_hdr*)buf;
				aah->len = htons((n*INTERP));
				aah->type = AUDIOSOCKET_TYPE_AUDIO;
				if (write(clifd, buf, (n*INTERP) + sizeof(struct asterisk_audiosock_hdr)) != ((n*INTERP) + sizeof(struct asterisk_audiosock_hdr))) {
					perror("write()");
					goto err_exit;
				}
			} else {
				printf("holding off on samples!\n");
			}
		}
	}

	int ret = 0;
	goto noerr_exit;
err_exit:
	ret = 1;
noerr_exit:
	modem_ipc[0] = 0;

	int modem_status;
	printf("waiting for modem process to die..\n");
	if (waitpid(modem_child, &modem_status, 0) < 0)
		if (errno != ECHILD)
			perror("waitpid()");
	
	modem_status = WEXITSTATUS(modem_status);

	printf("efax returned %d\n", (int)modem_ipc[2]);
	printf("modem child [%u] returned %d\n", modem_child, modem_status);
	
	if (modem_ipc[2] == 0) {
		struct dirent *dp;
		DIR* fax_dfd;
		char *filename_qfd;
		char *filename_tiff_rename;
		char *filename_png_rename;
		char *cmdline;
		int conv_errors = 0;
		if ((fax_dfd = opendir(fax_tdir)) != NULL) {
			printf("converting fax data...\n");
			while ((dp = readdir(fax_dfd)) != NULL) {
				struct stat stbuf;
				asprintf(&filename_qfd, "%s/%s",fax_tdir,dp->d_name);
				asprintf(&filename_tiff_rename, "%s.tiff", filename_qfd);
				asprintf(&filename_png_rename, "%s.png", filename_qfd);

				if(stat(filename_qfd,&stbuf ) == -1 ) {
					printf("Unable to stat file: %s\n",filename_qfd) ;
					continue ;
				}

				if ( ( stbuf.st_mode & S_IFMT ) == S_IFDIR ) continue;
				else {
					printf("-> converting %s\n", filename_qfd);
					if (rename(filename_qfd, filename_tiff_rename) < 0) perror("rename()");

					if (convert_image_tiff_png(filename_tiff_rename, filename_png_rename)) {
						fprintf(stderr, "Error converting %s\n", filename_qfd);
						conv_errors++;
					}
				}

				free(filename_qfd);
				free(filename_tiff_rename);
				free(filename_png_rename);
			}
		}

		printf("generating metadata..\n");
		printf("pulling cdr data\n");
		PGconn *conn = PQconnectdb(db_conninfo);

		if (PQstatus(conn) != CONNECTION_OK) {
			fprintf(stderr, "failed to connect to db, metadata will not be generated!\nerror: %s\n", PQerrorMessage(conn));
		} else {
			char* query;
			if (asprintf(&query, "select * from cdr where cdr.userfield = '%s'", uuid) < 0) {
				perror("asprintf()");
				exit(errno);
			}

			PGresult *res =  PQexec(conn, query);
			if (PQresultStatus(res) != PGRES_TUPLES_OK) {
				fprintf(stderr, "failed to execute SELECT query (%s)! (query = %s;)\n", PQerrorMessage(conn), query);
			} else {
				int field_clid = PQfnumber(res, "clid");
				int field_src = PQfnumber(res, "src");
				int field_dst = PQfnumber(res, "dst");
				int field_timestamp = PQfnumber(res, "calldate");
				int field_duration = PQfnumber(res, "duration");

				if (PQntuples(res) != 1 || field_clid < 0 || field_src < 0 || field_dst < 0 || field_timestamp < 0 || field_duration < 0) { // i don't know what this else/if error hell is but i'm too comitted to it now
					fprintf(stderr, "** failed to find CDR record (uuid=%s)!\n", uuid);
				} else {
					char* meta;
					char* meta_fname;
					if (asprintf(&meta, "UUID=%s\nCALLERID=%s\nSRC=%s\nDEST=%s\nTIMESTAMP=%s\nDURATION=%s\nPROCESSED=0\nCONVERRORS=%d\n", uuid,
							PQgetvalue(res, 0, field_clid), PQgetvalue(res, 0, field_src), PQgetvalue(res, 0, field_dst),
							PQgetvalue(res, 0, field_timestamp), PQgetvalue(res, 0, field_duration), conv_errors) < 0 
							|| asprintf(&meta_fname, "%s/_metadata", fax_tdir) < 0) {
						perror("asprintf()");
						ret = 1;
					}

					FILE* meta_fd = fopen(meta_fname, "w");
					if (meta_fd == NULL) {
						perror("fopen()");
						goto hard_error;
					}

					if (fwrite(meta, 1, strlen(meta) + 1, meta_fd) != strlen(meta) + 1) {
						perror("fwrite()");
					}
					fclose(meta_fd);

					free(meta);
					free(meta_fname);
				}
			}

			free(query);

			PQclear(res);
			PQfinish(conn);
		} 
	} else {
		char* meta;
		char* meta_fname;
		if (asprintf(&meta, "UUID=%s\nERROR=1", uuid) < 0 || asprintf(&meta_fname, "%s/_metadata", fax_tdir) < 0) {
			perror("asprintf()");
			ret = 1;
		}

		FILE* meta_fd = fopen(meta_fname, "w");
		if (fwrite(meta, 1, strlen(meta) + 1, meta_fd) != strlen(meta) + 1) {
			perror("fwrite()");
		}
		fclose(meta_fd);

		free(meta);
		free(meta_fname);
	}

	char *new_dirname;
	if (asprintf(&new_dirname, "%s/%s", inbound_path, uuid) < 0) {
		perror("asprintf()");
		goto hard_error;
	}

	if (got_uuid)
		if (rename(fax_tdir, new_dirname) < 0)
			perror("rename()");
	free(new_dirname);

	munmap(modem_ipc, sizeof(sig_atomic_t) * 3);
	modem_ipc = NULL;

hard_error:
	printf("waiting for all child processes to die\n");
	pid_t pid_dead;
	int late_status;
	while ((pid_dead = wait(&late_status)) > 0) { printf("%u died late with code %d\n", pid_dead, late_status); }

	close(clifd);

	close(asterisk_to_modem_pipe[1]);
	close(modem_to_asterisk_pipe[0]);

	printf("closed\n");
	return ret;
}