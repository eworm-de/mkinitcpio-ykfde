/*
 * (C) 2014 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * compile with:
 * $ gcc -o yk_test yk_test.c -lykpers-1 -lyubikey -liniparser
 *
 * test woth:
 * $ systemd-ask-password --no-tty "Please enter passphrase for disk foobar..."
 */

#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <yubikey.h>
#include <ykpers-1/ykdef.h>
#include <ykpers-1/ykcore.h>

#include <iniparser.h>

#define EVENT_SIZE	(sizeof (struct inotify_event))
#define EVENT_BUF_LEN	(1024 * (EVENT_SIZE + 16))

#define ASK_PATH	"/run/systemd/ask-password/"
#define ASK_MESSAGE	"Please enter passphrase for disk"

#define	CONFIGFILE	"/etc/ykfde.conf"
#define CHALLENGEFILE	"/ykfde-challenge"

static int send_on_socket(int fd, const char *socket_name, const void *packet, size_t size) {
        union {
                struct sockaddr sa;
                struct sockaddr_un un;
        } sa = {
                .un.sun_family = AF_UNIX,
        };

        strncpy(sa.un.sun_path, socket_name, sizeof(sa.un.sun_path));

        if (sendto(fd, packet, size, MSG_NOSIGNAL, &sa.sa, offsetof(struct sockaddr_un, sun_path) + strlen(socket_name)) < 0) {
                perror("sendto() failed");
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}

static int try_answer(char * ask_file, char * response) {
	int8_t ret = EXIT_FAILURE;
	dictionary * ini;
	char * ask_message, * ask_socket;
	int fd_askpass;

	if ((ini = iniparser_load(ask_file)) == NULL)
		perror("cannot parse file");

	ask_message = iniparser_getstring(ini, "Ask:Message", NULL);

	if (strncmp(ask_message, ASK_MESSAGE, strlen(ASK_MESSAGE)) != 0)
		goto out1;

	ask_socket = iniparser_getstring(ini, "Ask:Socket", NULL);

	if ((fd_askpass = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0)) < 0) {
		perror("socket() failed");
		goto out1;
	}

	if (send_on_socket(fd_askpass, ask_socket, response, strlen(response)) < 0) {
		perror("send_on_socket() failed");
		goto out2;
	}

	ret = EXIT_SUCCESS;

out2:
	close(fd_askpass);

out1:
	iniparser_freedict(ini);

	return ret;
}

int main(int argc, char **argv) {
	int8_t ret = EXIT_FAILURE;
	/* Yubikey */
	YK_KEY * yk;
	uint8_t slot = SLOT_CHAL_HMAC2;
	unsigned char response[SHA1_MAX_BLOCK_SIZE];
	unsigned char response_hex[(SHA1_MAX_BLOCK_SIZE * 2) + 1];
	char response_askpass[(SHA1_MAX_BLOCK_SIZE * 2) + 2];
	/* iniparser */
	dictionary * ini;
	/* read challenge */
	size_t fsize;
	char * challenge;
        FILE * challengefile;
	/* read dir */
	DIR * dir;
	struct dirent * ent;
	/* inotify */
	struct inotify_event * event;
	int fd_inotify, watch, length, i = 0;
	char buffer[EVENT_BUF_LEN];

	/* check if challenge file exists */
	if (access(CHALLENGEFILE, R_OK) == -1)
		goto out10;

	/* read challenge from file */
	if ((challengefile = fopen(CHALLENGEFILE, "r")) == NULL) {
		perror("Failed opening challenge file for reading");
		goto out10;
	}
	fseek(challengefile, 0, SEEK_END);
	fsize = ftell(challengefile);
	fseek(challengefile, 0, SEEK_SET);

	if ((challenge = malloc(fsize + 1)) == NULL) {
		perror("malloc() failed");
		goto out20;
	}

	if ((fread(challenge, fsize, 1, challengefile)) != 1) {
		perror("Failed reading challenge from file");
		goto out30;
	}
	challenge[fsize] = 0;
	/* finished challenge */

	/* try to read config file
	 * if anything here fails we do not care... slot 2 is the default */
	if ((ini = iniparser_load(CONFIGFILE)) != NULL) {
		slot = iniparser_getint(ini, "general:Slot", slot);

		switch (slot) {
			case '1':
				slot = SLOT_CHAL_HMAC1;
				break;
			default: /* slot 2 is default */
				slot = SLOT_CHAL_HMAC2;
				break;
		}

		iniparser_freedict(ini);
	}

	/* init and open Yubikey */
	if (!yk_init()) {
		perror("yk_init() failed");
		goto out30;
	}

	if ((yk = yk_open_first_key()) == NULL) {
		perror("yk_open_first_key() failed");
		goto out40;
	}

	memset(response, 0, sizeof(response));

	/* do challenge/response and encode to hex */
	if (!yk_challenge_response(yk, slot, 0, strlen(challenge), (unsigned char *)challenge, sizeof(response), response)) {
		perror("yk_challenge_response() failed");
		goto out50;
	}
	yubikey_hex_encode((char *)response_hex, (char *)response, 20);

	sprintf(response_askpass, "+%s", response_hex);

	/* change to directory so we do not have to assemble complete/absolute path */
	if (chdir(ASK_PATH) != 0) {
		perror("chdir() failed");
		goto out50;
	}

	/* creating the INOTIFY instance and add ASK_PATH directory into watch list */
	if ((fd_inotify = inotify_init()) < 0) {
		perror("inotify_init() failed");
		goto out50;
	}

	watch = inotify_add_watch(fd_inotify, ASK_PATH, IN_MOVED_TO);

	/* Is the request already there?
	 * We do this AFTER setting up the inotify watch. This way we do not have race condition. */
	if ((dir = opendir(ASK_PATH)) != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "ask.", 4) == 0) {
				if ((ret = try_answer(ent->d_name, response_askpass)) == EXIT_SUCCESS)
					goto out70;
			}
		}
	} else {
		perror ("opendir() failed");
		goto out60;
	}

	/* read to determine the event change happens. Actually this read blocks until the change event occurs */
	if ((length = read(fd_inotify, buffer, EVENT_BUF_LEN)) < 0) {
		perror("read() failed");
		goto out70;
	}

	/* actually read return the list of change events happens.
	 * Here, read the change event one by one and process it accordingly. */
	while (i < length) {
		event = (struct inotify_event *)&buffer[i];
		if (event->len > 0)
			if ((ret = try_answer(event->name, response_askpass)) == EXIT_SUCCESS)
				goto out70;
		i += EVENT_SIZE + event->len;
	}

out70:
	/* close dir */
	closedir(dir);

out60:
	/* remove inotify watch and remove file handle */
	inotify_rm_watch(fd_inotify, watch);
	close(fd_inotify);

out50:
	/* wipe response (cleartext password!) from memory */
	memset(response, 0, sizeof(response));
	memset(response_hex, 0, sizeof(response_hex));
	memset(response_askpass, 0, sizeof(response_askpass));

	/* close Yubikey */
	if (!yk_close_key(yk))
		perror("yk_close_key() failed");

out40:
	/* release Yubikey */
	if (!yk_release())
		perror("yk_release() failed");

out30:
	/* free challenge */
	free(challenge);

out20:
	/* close the challenge file */
	fclose(challengefile);
	/* Unlink it if we were successful, we can not try again later! */
	if (ret == EXIT_SUCCESS)
		unlink(CHALLENGEFILE);

out10:
	return ret;
}

// vim: set syntax=c:
