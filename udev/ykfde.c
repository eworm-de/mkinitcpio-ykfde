/*
 * (C) 2014-2015 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * compile with:
 * $ gcc -o ykfde ykfde.c -lykpers-1 -lyubikey -liniparser
 *
 * test woth:
 * $ systemd-ask-password --no-tty "Please enter passphrase for disk foobar..."
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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

#include "../config.h"

#define EVENT_SIZE	(sizeof (struct inotify_event))
#define EVENT_BUF_LEN	(1024 * (EVENT_SIZE + 16))

#define CHALLENGELEN	64
#define RESPONSELEN	SHA1_MAX_BLOCK_SIZE
#define PASSPHRASELEN	SHA1_DIGEST_SIZE * 2

#define ASK_PATH	"/run/systemd/ask-password/"
#define ASK_MESSAGE	"Please enter passphrase for disk"

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
	int8_t rc = EXIT_FAILURE;
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

	if (send_on_socket(fd_askpass, ask_socket, response, PASSPHRASELEN + 1) < 0) {
		perror("send_on_socket() failed");
		goto out2;
	}

	rc = EXIT_SUCCESS;

out2:
	close(fd_askpass);

out1:
	iniparser_freedict(ini);

	return rc;
}

int main(int argc, char **argv) {
	int8_t rc = EXIT_FAILURE;
	/* Yubikey */
	YK_KEY * yk;
	uint8_t slot = SLOT_CHAL_HMAC2;
	unsigned int serial = 0;
	char response[RESPONSELEN], passphrase[PASSPHRASELEN + 1], passphrase_askpass[PASSPHRASELEN + 2];
	/* iniparser */
	dictionary * ini;
	char section_ykslot[10 /* unsigned int in char */ + 1 + sizeof(CONFYKSLOT) + 1];
	/* read challenge */
	char challenge[CHALLENGELEN + 1];
	char challengefilename[sizeof(CHALLENGEDIR) + 11 /* "/challenge-" */ + 10 /* unsigned int in char */ + 1];
        int challengefile;
	/* read dir */
	DIR * dir;
	struct dirent * ent;
	/* inotify */
	struct inotify_event * event;
	int fd_inotify, watch, length, i = 0;
	char buffer[EVENT_BUF_LEN];

#if DEBUG
	/* reopening stderr to /dev/console may help debugging... */
	freopen("/dev/console", "w", stderr);
#endif

	memset(challenge, 0, CHALLENGELEN);

	/* init and open first Yubikey */
	if ((rc = yk_init()) < 0) {
		perror("yk_init() failed");
		goto out10;
	}

	if ((yk = yk_open_first_key()) == NULL) {
		rc = EXIT_FAILURE;
		perror("yk_open_first_key() failed");
		goto out20;
	}

	/* read the serial number from key */
	if((rc = !yk_get_serial(yk, 0, 0, &serial)) < 0) {
		perror("yk_get_serial() failed");
		goto out30;
	}

	sprintf(challengefilename, CHALLENGEDIR "/challenge-%d", serial);

	/* check if challenge file exists */
	if (access(challengefilename, R_OK) == -1)
		goto out30;

	/* read challenge from file */
	if ((rc = challengefile = open(challengefilename, O_RDONLY)) < 0) {
		perror("Failed opening challenge file for reading");
		goto out30;
	}

	if ((rc = read(challengefile, challenge, CHALLENGELEN)) < 0) {
		perror("Failed reading challenge from file");
		goto out50;
	}
	challengefile = close(challengefile);
	/* finished reading challenge */

	/* try to read config file
	 * if anything here fails we do not care... slot 2 is the default */
	if ((ini = iniparser_load(CONFIGFILE)) != NULL) {
		/* first try the general setting */
		slot = iniparser_getint(ini, "general:" CONFYKSLOT, slot);

		sprintf(section_ykslot, "%d:" CONFYKSLOT, serial);

		/* then probe for setting with serial number */
		slot = iniparser_getint(ini, section_ykslot, slot);

		switch (slot) {
			case 1:
			case SLOT_CHAL_HMAC1:
				slot = SLOT_CHAL_HMAC1;
				break;
			case 2:
			case SLOT_CHAL_HMAC2:
			default:
				slot = SLOT_CHAL_HMAC2;
				break;
		}

		iniparser_freedict(ini);
	}

	memset(response, 0, RESPONSELEN);
	memset(passphrase, 0, PASSPHRASELEN + 1);

	/* do challenge/response and encode to hex */
	if ((rc = yk_challenge_response(yk, slot, true,
					CHALLENGELEN, (unsigned char *)challenge,
					RESPONSELEN, (unsigned char *)response)) < 0) {
		perror("yk_challenge_response() failed");
		goto out50;
	}
	yubikey_hex_encode((char *)passphrase, (char *)response, 20);

	sprintf(passphrase_askpass, "+%s", passphrase);

	/* change to directory so we do not have to assemble complete/absolute path */
	if ((rc = chdir(ASK_PATH)) != 0) {
		perror("chdir() failed");
		goto out50;
	}

	/* creating the INOTIFY instance and add ASK_PATH directory into watch list */
	if ((rc = fd_inotify = inotify_init()) < 0) {
		perror("inotify_init() failed");
		goto out50;
	}

	watch = inotify_add_watch(fd_inotify, ASK_PATH, IN_MOVED_TO);

	/* Is the request already there?
	 * We do this AFTER setting up the inotify watch. This way we do not have race condition. */
	if ((dir = opendir(ASK_PATH)) != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "ask.", 4) == 0) {
				if ((rc = try_answer(ent->d_name, passphrase_askpass)) == EXIT_SUCCESS)
					goto out70;
			}
		}
	} else {
		rc = EXIT_FAILURE;
		perror ("opendir() failed");
		goto out60;
	}

	/* read to determine the event change happens. Actually this read blocks until the change event occurs */
	if ((rc = length = read(fd_inotify, buffer, EVENT_BUF_LEN)) < 0) {
		perror("read() failed");
		goto out70;
	}

	/* actually read return the list of change events happens.
	 * Here, read the change event one by one and process it accordingly. */
	while (i < length) {
		event = (struct inotify_event *)&buffer[i];
		if (event->len > 0)
			if ((rc = try_answer(event->name, passphrase_askpass)) == EXIT_SUCCESS)
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
	/* close the challenge file */
	if (challengefile)
		close(challengefile);
	/* Unlink it if we were successful, we can not try again later! */
	if (rc == EXIT_SUCCESS)
		unlink(challengefilename);

out30:
	/* wipe response (cleartext password!) from memory */
	memset(challenge, 0, CHALLENGELEN);
	memset(response, 0, RESPONSELEN);
	memset(passphrase, 0, PASSPHRASELEN + 1);
	memset(passphrase_askpass, 0, PASSPHRASELEN + 2);

	/* close Yubikey */
	if (yk_close_key(yk) < 0)
		perror("yk_close_key() failed");

out20:
	/* release Yubikey */
	if (yk_release() < 0)
		perror("yk_release() failed");

out10:
	return rc;
}

// vim: set syntax=c:
